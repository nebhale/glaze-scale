"""Run the native scale against a fresh, loopback-only Glaze Manager."""

import json
import os
from pathlib import Path
import signal
from collections import defaultdict
import socket
import subprocess
import sys
from tempfile import TemporaryDirectory
import threading
import time
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
MANAGER = Path(os.environ.get("GLAZE_MANAGER_DIR", ROOT.parent / "glaze-manager"))
KNOWN_TAG = "04A2B3C4D5E6F7"


def request(origin, method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = Request(origin + "/api/v1" + path, data=data, method=method,
                  headers={"Content-Type": "application/json"})
    with urlopen(req, timeout=5) as response:
        return json.load(response)


def seed(origin, jar_count=2):
    post = lambda path, body: request(origin, "POST", path, body)
    maker = post("/manufacturers", {"name": "AMACO"})
    glaze = post("/glazes", {"manufacturer_id": maker["id"], "name": "Seafoam over Blue Rutile",
                            "product_code": "PC-20", "cone": "06"})
    preset = post("/presets", {"name": "Pint jar", "tare_g": 250})
    fields = {"glaze_id": glaze["id"], "preset_id": preset["id"], "location": "Studio", "full_weight_g": 1050}
    jar = post("/containers", fields)
    jar = request(origin, "PUT", f"/tags/{KNOWN_TAG}/binding",
                  {"container_id": jar["id"], "expected_revision": jar["revision"]})
    post(f"/containers/{jar['id']}/measurements", {"expected_revision": jar["revision"], "gross_g": 774,
         "request_id": "simulator-previous-reading", "source": "web"})
    for _ in range(1, jar_count):
        post("/containers", fields)
    return jar["id"]


def exercise_retries(app, recovery):
    """Drop the first committed receipt and inject one conflict before setup."""
    from starlette.responses import JSONResponse

    submissions = defaultdict(list)

    @app.middleware("http")
    async def faults(req, call_next):
        path = req.url.path
        measurement = path.endswith("/measurements")
        setup = path.endswith("/jar-setups")
        if req.method != "POST" or not (measurement or setup):
            return await call_next(req)
        data = await req.json()
        if data.get("device_id") == "glaze-recovery-test":
            recovery["attempts"].append((path, data))
            if recovery["fault"] == "before":
                return JSONResponse({"detail": "Manager offline"}, status_code=503)
            response = await call_next(req)
            if recovery["fault"] == "after" and response.status_code == 200:
                return JSONResponse({"detail": "Lost receipt"}, status_code=503)
            return response
        if data.get("device_id") != "glaze-studio-test":
            return await call_next(req)
        attempts = submissions[path]
        attempts.append(data)
        if setup and len(attempts) == 1:
            return JSONResponse({"detail": "Jar changed"}, status_code=409)
        response = await call_next(req)
        if measurement and len(attempts) == 1 and response.status_code == 200:
            return JSONResponse({"detail": "Lost receipt"}, status_code=503)
        return response

    return submissions


def verify_recovery(origin, program, env, directory, recovery):
    def post(path, body):
        return request(origin, "POST", path, body)

    manufacturer = post("/manufacturers", {"name": "AMACO Brent"})
    glaze = post("/glazes", {"name": "Smokey Merlot", "manufacturer_id": manufacturer["id"],
                             "product_code": "PC-57", "cone": "5–6"})
    jar = post("/containers", {"glaze_id": glaze["id"], "tare_g": 250, "full_weight_g": 785,
                               "location": "Studio shelf"})
    request(origin, "PUT", "/tags/A1A2A3A4/binding",
            {"container_id": jar["id"], "expected_revision": jar["revision"]})
    for create in (False, True):
        prefs = Path(directory) / ("create-recovery" if create else "save-recovery")
        phase_env = {**env, "ESPHOME_PREFDIR": str(prefs)}
        if create:
            phase_env["GLAZE_TEST_CREATE"] = "1"
        def run_phase(phase):
            subprocess.run([str(program)], env={**phase_env, "GLAZE_TEST_RECOVERY": phase},
                           check=True, timeout=20)

        recovery["fault"] = "after" if create else "before"
        attempts = recovery["attempts"] = []
        run_phase("start")
        assert attempts
        count = len(attempts)
        recovery["fault"] = "before"
        run_phase("restart")
        assert len(attempts) > count
        count = len(attempts)
        recovery["fault"] = None
        run_phase("finish")
        assert len(attempts) == count + 1
        assert all(attempt == attempts[0] for attempt in attempts)
        count = len(attempts)
        run_phase("clean")
        assert len(attempts) == count

        linked = request(origin, "GET", "/tags/" + ("B1B2B3B4" if create else "A1A2A3A4"))["container"]
        container = request(origin, "GET", f"/containers/{linked['container_id']}")
        history = request(origin, "GET", f"/containers/{linked['container_id']}/measurements")
        assert len(history) == 1 and history[0]["gross_g"] == (600 if create else 475), history
        if create:
            assert container["location"] == "Kiln room" and float(container["cost_usd"]) == 15.50
            assert attempts[0][1]["new_glaze"]["name"] == "Mist Blue"
        else:
            assert linked["container_id"] == jar["id"] and linked["remaining_g"] == 225
    print("Verified one durable Save and Create through tag removal, tag replacement and process restarts.")


def main():
    import uvicorn

    with TemporaryDirectory(prefix="glaze-scale-") as directory, socket.socket() as sock:
        os.environ["GLAZE_DATA_DIR"] = directory
        sys.path.insert(0, str(MANAGER))
        from backend.app import app

        test = sys.argv[1:] == ["--test"]
        recovery = {}
        attempts = exercise_retries(app, recovery) if test else {}
        sock.bind(("127.0.0.1", 0))
        origin = f"http://127.0.0.1:{sock.getsockname()[1]}"
        server = uvicorn.Server(uvicorn.Config(app, log_level="warning"))
        thread = threading.Thread(target=server.run, kwargs={"sockets": [sock]}, daemon=True)
        thread.start()
        try:
            while not server.started:
                if not thread.is_alive():
                    raise RuntimeError("Glaze Manager failed to start")
                time.sleep(0.05)
            known_id = seed(origin, jar_count=9 if test else 2)
            name = "glaze-studio-test" if test else "glaze-scale-sim"
            build_root = ROOT / ("tests" if test else "") / ".esphome/build" / name
            program = build_root / ".pioenvs" / name / "program"
            env = {**os.environ, "GLAZE_SIM_ORIGIN": origin}
            if test:
                env["SDL_VIDEODRIVER"] = "dummy"
                env["ESPHOME_PREFDIR"] = directory
            result = subprocess.run([str(program)], env=env, timeout=150 if test else None)
            if test and result.returncode == 0:
                subprocess.run([str(program)], env={**env, "GLAZE_TEST_DISPLAY_RESTART": "1"},
                               timeout=10, check=True)
                weights = attempts[f"/api/v1/containers/{known_id}/measurements"]
                setups = attempts["/api/v1/device/jar-setups"]
                assert len(weights) == 2 and weights[0] == weights[1], weights
                assert len(setups) == 2 and setups[0]["request_id"] != setups[1]["request_id"], setups
                history = request(origin, "GET", f"/containers/{known_id}/measurements")
                assert sorted(item["gross_g"] for item in history) == [756, 774], history
                created = request(origin, "GET", "/tags/04010203")["container"]
                assert created["number"] == 10 and created["gross_g"] == 600, created
                linked = request(origin, "GET", "/tags/ABCDEF12")["container"]
                assert linked["number"] == 2 and linked["gross_g"] == 350, linked
                removed = request(origin, "GET", "/device/jars/3")
                retries = attempts[f"/api/v1/containers/{removed['container_id']}/measurements"]
                assert len(retries) == 2 and retries[0] == retries[1] and retries[0]["gross_g"] == 350, retries
                print("Verified saved readings, linked jar, and atomic setup in Glaze Manager.")
                verify_recovery(origin, program, env, directory, recovery)
            return 0 if result.returncode == -signal.SIGINT and not test else result.returncode
        finally:
            server.should_exit = True
            thread.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
