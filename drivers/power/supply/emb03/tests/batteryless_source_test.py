#!/usr/bin/env python3
"""Source-contract smoke checks only; not a hardware or fault-injection test.
Do not execute until independent Gemini review clears the exact candidate.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
POLICY = (ROOT / "emb03-charge-policy.c").read_text()
PROVIDER = (ROOT / "emb03-charge-provider.c").read_text()
CW = (ROOT / "cw201x-emb03.c").read_text()


def body(source, name):
    start = source.index("\n{", source.index(name + "(")) + 2
    depth = 1
    end = start
    while depth:
        end += 1
        depth += (source[end] == "{") - (source[end] == "}")
    return source[start:end]


class BatterylessSourceContract(unittest.TestCase):
    def test_opt_in_and_stop_before_publish(self):
        probe = body(PROVIDER, "emb03_sy_probe")
        self.assertTrue('"factory48,batteryless"' in probe or '"polyhex,batteryless"' in probe)
        self.assertLess(probe.index("emb03_charge_inhibit(charge)"),
                        probe.index("provider = charge"))

    def test_persistent_policy_guards(self):
        for name, first_io in (("emb03_charge_open", "emb03_read_pair"),
                               ("emb03_charge_set_level", "emb03_write_pair")):
            code = body(POLICY, name)
            self.assertLess(code.index("if (c->batteryless)"), code.index(first_io))
            self.assertIn("ret = -EPERM", code)
        self.assertNotIn("batteryless", body(POLICY, "emb03_charge_invalidate"))

    def test_two_stop_mechanisms(self):
        code = body(POLICY, "emb03_charge_inhibit")
        self.assertIn("emb03_write_verify(c, 0x12, option | 1, 1)", code)
        self.assertIn("current_ret = emb03_write_verify(c, 0x14, 0, 0xffff)", code)
        between = code.split("option | 1, 1);")[1].split("current_ret =")[0]
        self.assertNotIn("goto", between)
        self.assertNotIn("return", between)
        verify = body(POLICY, "emb03_write_verify")
        self.assertLess(verify.index("rd = emb03_read_value"),
                        verify.index("if (wr < 0)"))
        self.assertIn("actual & mask", verify)

    def test_cw_bypasses(self):
        for name, operation in (("cw2015_parse_dt", "of_find_property"),
                                ("cw_init", "cw_write"),
                                ("cw_por", "cw_write"),
                                ("cw_update_config_info", "cw_read"),
                                ("cw_bat_work", "cw_read")):
            code = body(CW, name)
            self.assertLess(code.index("emb03_charge_is_batteryless"),
                            code.index(operation))
        props = body(CW, "cw_batteryless_get_property")
        self.assertIn("val->intval = 100;", props)
        self.assertIn("POWER_SUPPLY_STATUS_FULL", props)
        self.assertIn("POWER_SUPPLY_PROP_PRESENT:\n\t\tval->intval = 1;", props)
        self.assertIn("get_charge_state(cw)", body(CW, "emb03_charger_get_property"))

    def test_lifecycle(self):
        for name in ("emb03_sy_remove", "emb03_sy_shutdown", "emb03_sy_suspend"):
            self.assertIn("emb03_sy_stop_work(charge)", body(PROVIDER, name))
        resume = body(PROVIDER, "emb03_sy_resume")
        self.assertLess(resume.index("emb03_charge_inhibit"),
                        resume.index("emb03_sy_start_work"))
        self.assertIn("cancel_delayed_work_sync", body(PROVIDER, "emb03_sy_stop_work"))


if __name__ == "__main__":
    unittest.main()
