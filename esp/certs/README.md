# ルート証明書

`roots.pem`はcertifiのMozilla CAバンドルを変更せずコピーしたものです。出典は[certifi](https://github.com/certifi/python-certifi)で、証明書部分は同梱の`LICENSE`（MPL-2.0）に従います。

更新時は信頼できる環境の最新版certifiから`cacert.pem`とライセンスをコピーし、出典の版・SHA-256を更新して両ボードをビルドします。証明書の有効期限・失効やAPI側のチェーン変更は、実APIへのTLS疎通でも確認してください。秘密鍵は含みません。

同梱版: certifi 2026.10.23

SHA-256: `2ef52ad56b4803e264045c915e55c236cbfa045b84b81efefc218a19eb44bd2b`
