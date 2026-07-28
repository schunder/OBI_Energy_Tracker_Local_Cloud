#!/usr/bin/env python3
"""
fetch_device_provisioning.py -- get the AWS-IoT fleet-provisioning cert bundle for one of YOUR
bridges from the OBI cloud (POST /device-provisionings), and save it ready for obi_ota_download.py.

Unlike /bluetooth-challenges (TEA key), this endpoint is expected to check that the bridge is
actually registered to your account -- so it only works for a bridge your OBI login owns.

    python fetch_device_provisioning.py --uuid <bridge-uuid> --out-dir certs_bridge

Then:
    python obi_ota_download.py --from-dir certs_bridge --uuid <bridge-uuid> --out stock_backup.bin

No third-party packages needed (Python standard library only).
"""
import argparse
import getpass
import json
import os
import sys
import urllib.request
import urllib.error

LOGIN_URL = "https://www.obi.de/regi/auth/api/public/login"
API = "https://energy-tracking-backend.prod-eks.dbs.obi.solutions"


def _post(url, body, headers):
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        url, data=data, method="POST",
        headers={"Content-Type": "application/json", "Accept-Encoding": "identity", **headers},
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        sys.exit("HTTP %s from %s: %s" % (e.code, url, e.read().decode(errors="replace")[:300]))
    except urllib.error.URLError as e:
        sys.exit("network error: %s" % e.reason)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--uuid", required=True, help="bridge UUID (as read via UART cmd 49 / the app)")
    ap.add_argument("--out-dir", default="certs_bridge", help="directory to write ca.pem/client.crt/client.key/thing.json")
    args = ap.parse_args()

    print("Fetch this bridge's cloud provisioning cert (needs an OBI login that OWNS this bridge).\n")
    email = input("OBI account email:    ").strip()
    password = getpass.getpass("OBI account password: ")

    tok = _post(LOGIN_URL,
                {"email": email, "password": password, "country": "DE"},
                {"x-app-type": "b2c", "x-obi-locale": "de-DE",
                 "User-Agent": "heyOBI APP / Android Phone 30"})
    token = tok.get("token")
    if not token:
        sys.exit("login failed: no token in response")

    res = _post(API + "/device-provisionings",
                {"bridgeId": args.uuid},
                {"Authorization": "Bearer " + token,
                 "Accept": "application/vnd.obi.companion.energy-tracking.device-provisioning.v1+json",
                 "User-Agent": "app_client"})

    ca = res.get("caPem")
    cert = res.get("certificatePem")
    key = res.get("privateKey")
    endpoint = res.get("clusterEndpointUri")
    if not (ca and cert and key):
        sys.exit("no cert bundle returned (bridge not registered to this account?). Raw: %r" % (res,))

    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "ca.pem"), "w") as f:
        f.write(ca)
    with open(os.path.join(args.out_dir, "client.crt"), "w") as f:
        f.write(cert)
    with open(os.path.join(args.out_dir, "client.key"), "w") as f:
        f.write(key)
    with open(os.path.join(args.out_dir, "thing.json"), "w") as f:
        json.dump({"thingName": args.uuid, "clusterEndpointUri": endpoint}, f, indent=2)

    print(f"\nWrote ca.pem / client.crt / client.key / thing.json -> {args.out_dir}/")
    print(f"clusterEndpointUri: {endpoint}")
    print(f"\nNext: python obi_ota_download.py --from-dir {args.out_dir} --uuid {args.uuid} "
          f"--endpoint {endpoint} --out stock_backup.bin")


if __name__ == "__main__":
    main()
