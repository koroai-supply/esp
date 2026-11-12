from dataclasses import dataclass
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import ssl
import tempfile
import time
import urllib.error
import urllib.request

KEY = re.compile(r"kr_(test|live)_[A-Za-z0-9_-]{43}\Z")
ID = re.compile(r"[0-9a-f]{32}\Z")
UUID = re.compile(r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\Z")


def valid_jan(jan):
    return (isinstance(jan, str) and len(jan) in (8, 13)
            and all('0' <= c <= '9' for c in jan)
            and sum(int(c) * (3 if (len(jan) - 1 - i) % 2 else 1)
                    for i, c in enumerate(jan)) % 10 == 0)


def valid_pending(p):
    if p.get('quick') is True:
        return 'jan' not in p and type(p.get('qty')) is int and p['qty'] == 1
    return ('quick' not in p and valid_jan(p.get('jan')) and type(p.get('qty')) is int
            and 1 <= p['qty'] <= 10)


@dataclass(frozen=True)
class Reply:
    """outcome: accepted, retry, blocked, or not_set (quick_order_not_set: no order made)."""
    outcome: str = "retry"
    http_status: int = 0
    retry_after: int = 60
    order_id: str = ""
    jan: str = ""


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HttpsTransport:
    def __init__(self, api_key):
        self.api_key = api_key
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect(),
            urllib.request.HTTPSHandler(context=ssl.create_default_context()))

    def __call__(self, pending):
        quick = pending.get('quick') is True
        request = urllib.request.Request(
            'https://api.koroai.supply/v1/quick-order' if quick else 'https://api.koroai.supply/v1/orders',
            method='POST',
            data=b'{}' if quick else json.dumps({'jan': pending['jan'], 'qty': pending['qty']}).encode(),
            headers={'Authorization': 'Bearer ' + self.api_key,
                     'Content-Type': 'application/json',
                     'Idempotency-Key': pending['key']})
        try:
            response = self.opener.open(request, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        except (OSError, urllib.error.URLError, http.client.HTTPException):
            return Reply()
        with response:
            status = response.code
            if status == 429:
                value = response.headers.get('Retry-After', '60')
                seconds = min(int(value), 2**32 - 1) if re.fullmatch(r'[0-9]{1,10}', value) else 2**32 - 1
                return Reply('retry', status, max(60, seconds))
            if status == 422 and quick and self._not_set(response):
                return Reply('not_set', status)
            if status != 201:
                return Reply('retry' if status >= 500 or status == 408 else 'blocked', status)
            try:
                body = response.read(4097)
                if len(body) > 4096 or response.headers.get_content_type() != 'application/json':
                    return Reply('retry', status)
                data = json.loads(body)['data']
                mode = 'sandbox' if self.api_key.startswith('kr_test_') else 'live'
                if (not isinstance(data, dict) or not isinstance(data.get('id'), str)
                        or not UUID.fullmatch(data['id']) or data.get('mode') != mode
                        or not (valid_jan(data.get('jan')) if quick else data.get('jan') == pending['jan'])
                        or type(data.get('qty')) is not int
                        or data['qty'] != pending['qty']
                        or data.get('status') not in (('simulated', 'canceled') if mode == 'sandbox' else ('accepted', 'canceled'))):
                    return Reply('retry', status)
                return Reply('accepted', status, order_id=data['id'], jan=data['jan'])
            except (ValueError, KeyError, TypeError, OSError, http.client.HTTPException):
                return Reply('retry', status)

    @staticmethod
    def _not_set(response):
        try:
            body = response.read(4097)
            return (len(body) <= 4096 and response.headers.get_content_type() == 'application/json'
                    and json.loads(body).get('error') == 'quick_order_not_set')
        except (ValueError, AttributeError, OSError, http.client.HTTPException):
            return False

    def quick_order_product(self):
        """GET /v1/quick-order. The product dict, or None when the home has none set.

        Raises OSError on network errors or unexpected responses. Never orders.
        """
        request = urllib.request.Request(
            'https://api.koroai.supply/v1/quick-order',
            headers={'Authorization': 'Bearer ' + self.api_key})
        try:
            response = self.opener.open(request, timeout=15)
        except urllib.error.HTTPError as error:
            error.close()
            raise OSError(f'quick-order lookup failed: HTTP {error.code}') from None
        except (urllib.error.URLError, http.client.HTTPException) as error:
            raise OSError('quick-order lookup failed') from error
        with response:
            try:
                body = response.read(4097)
                if (response.code != 200 or len(body) > 4096
                        or response.headers.get_content_type() != 'application/json'):
                    raise ValueError
                product = json.loads(body)['data']['product']
                if product is not None and (not isinstance(product, dict) or not valid_jan(product.get('jan'))
                                            or not isinstance(product.get('name'), str)):
                    raise ValueError
                return product
            except (ValueError, KeyError, TypeError, http.client.HTTPException):
                raise OSError('quick-order lookup returned an invalid response') from None


class Client:
    """Hold the process lock for the client's entire lifetime. Use as a context manager.

    No new order until the prior result is acknowledged. A storage error poisons
    this instance; close and reopen to recover from the durable record.
    """
    def __init__(self, api_key, state_dir, *, transport=None, clock=time.time):
        if not isinstance(api_key, str) or not KEY.fullmatch(api_key):
            raise ValueError('invalid API_KEY')
        self.scope = hashlib.sha256(api_key.encode()).hexdigest()
        self.transport = transport if transport is not None else HttpsTransport(api_key)
        self.clock = clock
        self.directory = Path(state_dir)
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.path = self.directory / 'journal.json'
        self.lock = os.open(self.directory / 'journal.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        self.failed = False
        self.accepted = False
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            try:
                fd = os.open(self.path, os.O_RDONLY | os.O_NOFOLLOW)
            except FileNotFoundError:
                self.journal = dict(version=1, installation=secrets.token_hex(16), sequence=0,
                                    pending=None, blocked=False, retry_at=0, retry_delay=60)
                self._save()
            else:
                with os.fdopen(fd) as source:
                    self.journal = json.loads(source.read(4097))
                self._validate()
        except BaseException:
            self.close()
            raise

    def _validate(self):
        j = self.journal
        if (not isinstance(j, dict) or j.get('version') != 1
                or not isinstance(j.get('installation'), str) or not ID.fullmatch(j['installation'])
                or type(j.get('sequence')) is not int or not 0 <= j['sequence'] < 2**64
                or type(j.get('blocked')) is not bool or type(j.get('retry_at')) is not int
                or j['retry_at'] < 0 or type(j.get('retry_delay')) is not int
                or not 60 <= j['retry_delay'] <= 3600 or 'pending' not in j):
            raise ValueError('invalid journal; stopped')
        p = j['pending']
        if p is not None and (not isinstance(p, dict) or p.get('scope') != self.scope
                              or not valid_pending(p) or j['sequence'] == 0
                              or p.get('key') != f"dash-{j['installation']}-{j['sequence']}"):
            raise ValueError('pending order or credential mismatch; stopped')

    def _save(self):
        temporary = None
        try:
            fd, temporary = tempfile.mkstemp(prefix='.journal-', dir=self.directory)
            with os.fdopen(fd, 'w') as target:
                json.dump(self.journal, target, separators=(',', ':'))
                target.flush()
                os.fsync(target.fileno())
            os.replace(temporary, self.path)
            directory = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        except BaseException:
            self.failed = True
            raise
        finally:
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)

    def _check(self):
        if self.lock is None or self.failed:
            raise RuntimeError('client stopped; reopen to recover')

    @property
    def pending(self):
        p = self.journal['pending']
        return dict(p) if p is not None else None

    @property
    def blocked(self):
        return self.journal['blocked']

    @property
    def retry_at(self):
        return self.journal['retry_at']

    def start(self, jan, qty=1):
        self._check()
        if not valid_jan(jan) or type(qty) is not int or not 1 <= qty <= 10:
            raise ValueError('invalid JAN or qty')
        return self._start(dict(jan=jan, qty=qty))

    def start_quick(self):
        """Store one package of the home's quick-order product; the server picks it."""
        self._check()
        return self._start(dict(quick=True, qty=1))

    def _start(self, order):
        if self.pending is not None or self.blocked:
            raise RuntimeError('an order is already pending or blocked')
        if self.journal['sequence'] == 2**64 - 1:
            raise RuntimeError('order sequence exhausted')
        self.journal['sequence'] += 1
        self.journal['pending'] = dict(order, scope=self.scope,
                                      key=f"dash-{self.journal['installation']}-{self.journal['sequence']}")
        self.journal['retry_at'] = 0
        self.journal['retry_delay'] = 60
        self.accepted = False
        self._save()
        return self.pending['key']

    def attempt(self):
        self._check()
        now = int(self.clock())
        if self.pending is None or self.blocked or self.accepted or now < self.retry_at:
            return None
        self.journal['retry_at'] = now + self.journal['retry_delay']
        self._save()
        reply = self.transport(self.pending)
        if reply.outcome not in ('accepted', 'retry', 'blocked', 'not_set'):
            raise ValueError('invalid transport result')
        if reply.outcome == 'not_set':
            if self.pending.get('quick') is True:
                # No order was created. Drop this event; its sequence is never reused.
                self.journal.update(pending=None, retry_at=0, retry_delay=60)
                self._save()
                return reply
            reply = Reply('blocked', reply.http_status)
        self.accepted = reply.outcome == 'accepted'
        self.journal['blocked'] = reply.outcome == 'blocked'
        self.journal['retry_at'] = max(now + 60, int(self.clock()) + max(self.journal['retry_delay'], reply.retry_after))
        if reply.outcome == 'retry':
            self.journal['retry_delay'] = min(3600, self.journal['retry_delay'] * 2)
        self._save()
        return reply

    def acknowledge(self):
        self._check()
        if not self.accepted:
            raise RuntimeError('no accepted order to acknowledge')
        self.journal['pending'] = None
        self._save()
        self.accepted = False

    def close(self):
        if self.lock is not None:
            os.close(self.lock)
            self.lock = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
