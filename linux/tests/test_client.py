import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from email.message import Message

from koroai_hw import Client, Reply, valid_jan
from koroai_hw.client import HttpsTransport

KEY = 'kr_test_' + 'a'*43
JAN = '4901234567894'


class ClientTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.now = 100
        self.sent = []
        self.reply = Reply()

    def open(self, key=KEY):
        def send(pending):
            disk = json.loads((Path(self.temp.name)/'journal.json').read_text())
            self.assertEqual(disk['pending'], pending)
            self.sent.append(pending)
            return self.reply
        return Client(key, self.temp.name, transport=send, clock=lambda: self.now)

    def test_validation(self):
        for jan in (JAN, '96385074', '01234565'):
            self.assertTrue(valid_jan(jan))
        for jan in ('4901234567893', '', '１２３４５６７８', 12345678):
            self.assertFalse(valid_jan(jan))
        with self.open() as client:
            for qty in (0, 11, True, 1.5):
                with self.assertRaises(ValueError):
                    client.start(JAN, qty)

    def test_reboot_before_and_after_ack(self):
        with self.open() as client:
            key = client.start(JAN)
            self.assertTrue(key.endswith('-1'))
            client.attempt()
            with self.assertRaises(RuntimeError):
                client.start(JAN)
        with self.open() as client:
            self.assertEqual(client.pending['key'], key)
            self.assertIsNone(client.attempt())
            self.now = 160
            self.reply = Reply('accepted', 201)
            client.attempt()
        with self.open() as client:
            self.now = 280
            client.attempt()
            self.assertEqual(client.pending['key'], key)
            client.acknowledge()
        with self.open() as client:
            second = client.start(JAN)
            self.assertEqual(second.rsplit('-',1)[0], key.rsplit('-',1)[0])
            self.assertTrue(second.endswith('-2'))
        self.assertEqual([p['key'] for p in self.sent], [key]*3)

    def test_backoff_survives_restart(self):
        with self.open() as client:
            client.start(JAN)
            client.attempt()
        with self.open() as client:
            self.now = 160
            client.attempt()
            self.assertEqual(client.retry_at, 280)
            self.now = 279
            self.assertIsNone(client.attempt())
        with self.open() as client:
            self.now = 280
            client.attempt()
            self.assertEqual(client.retry_at, 520)

    def test_process_lock(self):
        with self.open():
            with self.assertRaises(BlockingIOError):
                self.open()

    def test_corruption_fails_closed(self):
        with self.open() as client:
            client.start(JAN)
        (Path(self.temp.name)/'journal.json').write_text('{broken')
        with self.assertRaises(ValueError):
            self.open()

    def test_key_change_stops_pending(self):
        with self.open() as client:
            client.start(JAN)
        with self.assertRaises(ValueError):
            self.open('kr_live_'+'b'*43)
        self.assertEqual(self.sent, [])

    def test_failed_persistence_stops_before_send(self):
        with self.open() as client:
            with patch('os.replace', side_effect=OSError('disk')):
                with self.assertRaises(OSError):
                    client.start(JAN)
            with self.assertRaises(RuntimeError):
                client.attempt()
        self.assertEqual(self.sent, [])

    def test_failed_fence_and_ack(self):
        with self.open() as client:
            key = client.start(JAN)
            with patch('os.fsync', side_effect=OSError('disk')):
                with self.assertRaises(OSError):
                    client.attempt()
        self.assertEqual(self.sent, [])
        with self.open() as client:
            self.reply = Reply('accepted', 201)
            client.attempt()
            with patch('os.replace', side_effect=OSError('disk')):
                with self.assertRaises(OSError):
                    client.acknowledge()
        with self.open() as client:
            self.assertEqual(client.pending['key'], key)

    def test_retry_after_and_rejection_persist(self):
        with self.open() as client:
            client.start(JAN)
            self.reply = Reply('retry', 429, 3600)
            client.attempt()
        with self.open() as client:
            self.now = 3699
            self.assertIsNone(client.attempt())
            self.now = 3700
            self.reply = Reply('blocked', 409)
            client.attempt()
        with self.open() as client:
            self.assertTrue(client.blocked)
            self.assertIsNone(client.attempt())
            with self.assertRaises(RuntimeError):
                client.acknowledge()

    def test_returned_pending_cannot_mutate_record(self):
        with self.open() as client:
            client.start(JAN)
            client.pending['key'] = 'wrong'
            self.assertNotEqual(client.pending['key'], 'wrong')

    def test_quick_order(self):
        with self.open() as client:
            key = client.start_quick()
            self.assertTrue(key.endswith('-1'))
            self.assertEqual(client.pending, {'quick': True, 'qty': 1, 'scope': client.scope, 'key': key})
            with self.assertRaises(RuntimeError):
                client.start_quick()  # held or repeated press while pending
            client.attempt()  # lost response
        with self.open() as client:
            self.assertEqual(client.pending['key'], key)
            self.now = 160
            self.reply = Reply('accepted', 201, order_id='x', jan=JAN)
            self.assertEqual(client.attempt().jan, JAN)
            client.acknowledge()
            self.reply = Reply('not_set', 422)
            second = client.start_quick()
            self.assertEqual(client.attempt().outcome, 'not_set')
            self.assertIsNone(client.pending)
            self.assertFalse(client.blocked)
            self.assertEqual((client.retry_at, client.journal['retry_delay']), (0, 60))
        with self.open() as client:
            self.assertIsNone(client.pending)
            third = client.start_quick()
            self.assertTrue(second.endswith('-2') and third.endswith('-3'))
        self.assertEqual([p['key'] for p in self.sent], [key, key, second])
        self.assertTrue(all(p.get('quick') is True and 'jan' not in p for p in self.sent))

    def test_not_set_for_jan_order_stops(self):
        with self.open() as client:
            client.start(JAN)
            self.reply = Reply('not_set', 422)
            self.assertEqual(client.attempt().outcome, 'blocked')
            self.assertTrue(client.blocked)
            self.assertIsNotNone(client.pending)

    def test_corrupt_quick_record_fails_closed(self):
        with self.open() as client:
            client.start_quick()
        path = Path(self.temp.name)/'journal.json'
        for change in ({'jan': JAN}, {'qty': 2}, {'quick': 1}):
            journal = json.loads(path.read_text())
            original = dict(journal['pending'])
            journal['pending'].update(change)
            path.write_text(json.dumps(journal))
            with self.assertRaises(ValueError):
                self.open()
            journal['pending'] = original
            path.write_text(json.dumps(journal))


class Response(io.BytesIO):
    def __init__(self, body, status=201, retry='60', content='application/json'):
        super().__init__(body)
        self.code = status
        self.headers = Message()
        self.headers['Content-Type'] = content
        self.headers['Retry-After'] = retry


class TransportTest(unittest.TestCase):
    def setUp(self):
        self.transport = HttpsTransport(KEY)
        self.pending = {'key':'dash-event-1','jan':JAN,'qty':1}
        self.data = {'id':'11111111-1111-4111-8111-111111111111','mode':'sandbox',
                     'jan':JAN,'qty':1,'status':'simulated'}

    def send(self, response):
        with patch.object(self.transport.opener, 'open', return_value=response) as request:
            result = self.transport(self.pending)
            sent = request.call_args.args[0]
            self.assertEqual(sent.get_header('Idempotency-key'), 'dash-event-1')
            self.assertEqual(sent.full_url, 'https://api.koroai.supply/v1/orders')
            self.assertEqual(json.loads(sent.data), {'jan':JAN,'qty':1})
            return result

    def test_valid_order(self):
        self.assertEqual(self.send(Response(json.dumps({'data':self.data}).encode())).outcome, 'accepted')

    def test_ambiguous_success_not_acknowledged(self):
        for field, value in [('qty',True),('jan','96385074'),('mode','live'),('id','invalid')]:
            data = dict(self.data, **{field:value})
            self.assertEqual(self.send(Response(json.dumps({'data':data}).encode())).outcome, 'retry')
        for body in (b'{broken', b'x'*4097):
            self.assertEqual(self.send(Response(body)).outcome, 'retry')

    def test_http_classification(self):
        for status in (301, 403, 404, 409, 422):
            self.assertEqual(self.send(Response(b'',status)).outcome,'blocked')
        for status in (408,500,502,503,504):
            self.assertEqual(self.send(Response(b'',status)).outcome,'retry')
        self.assertEqual(self.send(Response(b'',429,'3600')).retry_after,3600)
        self.assertEqual(self.send(Response(b'',429,'oops')).retry_after,2**32-1)

    def test_quick_order_request(self):
        pending = {'quick': True, 'qty': 1, 'key': 'dash-event-2'}
        data = dict(self.data, jan='96385074')
        with patch.object(self.transport.opener, 'open',
                          return_value=Response(json.dumps({'data': data}).encode())) as request:
            result = self.transport(pending)
            sent = request.call_args.args[0]
            self.assertEqual(sent.full_url, 'https://api.koroai.supply/v1/quick-order')
            self.assertEqual(sent.data, b'{}')
            self.assertEqual(sent.get_header('Idempotency-key'), 'dash-event-2')
        self.assertEqual((result.outcome, result.jan), ('accepted', '96385074'))
        for field, value in [('qty', 2), ('jan', '96385075'), ('mode', 'live')]:
            body = json.dumps({'data': dict(data, **{field: value})}).encode()
            with patch.object(self.transport.opener, 'open', return_value=Response(body)):
                self.assertEqual(self.transport(pending).outcome, 'retry')
        cases = [(b'{"error":"quick_order_not_set"}', 'not_set'),
                 (b'{"error":"product_not_available"}', 'blocked'), (b'{broken', 'blocked')]
        for body, outcome in cases:
            with patch.object(self.transport.opener, 'open', return_value=Response(body, 422)):
                self.assertEqual(self.transport(pending).outcome, outcome)
        # A JAN order never treats this error as "not set".
        self.assertEqual(self.send(Response(b'{"error":"quick_order_not_set"}', 422)).outcome, 'blocked')

    def test_quick_order_product(self):
        product = {'jan': JAN, 'name': '水', 'unit_price_jpy': 100, 'purchase_unit': 'package'}
        for value in (product, None):
            body = json.dumps({'data': {'product': value}}).encode()
            with patch.object(self.transport.opener, 'open', return_value=Response(body, 200)) as request:
                self.assertEqual(self.transport.quick_order_product(), value)
                sent = request.call_args.args[0]
                self.assertEqual((sent.get_method(), sent.full_url),
                                 ('GET', 'https://api.koroai.supply/v1/quick-order'))
        for body in (b'{broken', b'{"data":{"product":{"jan":"1"}}}', b'{}'):
            with patch.object(self.transport.opener, 'open', return_value=Response(body, 200)):
                with self.assertRaises(OSError):
                    self.transport.quick_order_product()
        with patch.object(self.transport.opener, 'open', side_effect=TimeoutError()):
            with self.assertRaises(OSError):
                self.transport.quick_order_product()

    def test_lost_response(self):
        with patch.object(self.transport.opener,'open',side_effect=TimeoutError()):
            self.assertEqual(self.transport(self.pending).outcome,'retry')


if __name__ == '__main__':
    unittest.main()
