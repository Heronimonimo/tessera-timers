"""Timers: a kitchen timer, a stopwatch, and Assist voice timers on the glass.

The timers and stopwatches that run on the screen live in the component (timers.cpp), one slot per tile of the layout,
and are kept in the screen's flash so a restart picks them up where they were. A timer tile that belongs to a timer
helper of Home Assistant starts, pauses and cancels that one instead. When `voice_assistant` is configured on the
same screen (for example via the `voice_assist` plugin), this component enables Assist voice timers (`FEATURE_TIMERS`)
and tracks them automatically.
"""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, number, smart_display, speaker, switch, text_sensor
from esphome.const import CONF_ID
from esphome.core import CORE, CoroPriority, coroutine_with_priority

DEPENDENCIES = ["smart_display", "binary_sensor", "switch", "text_sensor", "button"]

CONF_DONE = "done"
CONF_POPUP = "popup_when_done"
CONF_OPENS = "opens_when_done"
CONF_DONE_LIST = "done_list"
CONF_SPEAKER = "speaker"
CONF_SOUND_ON = "sound_on"
CONF_ALARM_VOLUME = "alarm_volume"
DONE = Path(__file__).parent / "sounds" / "done.wav"

timers_ns = cg.esphome_ns.namespace("timers")
Timers = timers_ns.class_("Timers", cg.Component)


def check_sound(config):
    if CONF_SPEAKER in config:
        smart_display.read_sound(DONE)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Timers),
            cv.Required(CONF_DONE): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_POPUP): cv.use_id(switch.Switch),
            cv.Required(CONF_OPENS): cv.use_id(switch.Switch),
            cv.Optional(CONF_DONE_LIST): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
            cv.Optional(CONF_SOUND_ON): cv.use_id(switch.Switch),
            cv.Optional(CONF_ALARM_VOLUME): cv.use_id(number.Number),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    check_sound,
)


@coroutine_with_priority(CoroPriority.LATE)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_done(await cg.get_variable(config[CONF_DONE])))
    if CONF_POPUP in config:
        cg.add(var.set_popup(await cg.get_variable(config[CONF_POPUP])))
    cg.add(var.set_opens(await cg.get_variable(config[CONF_OPENS])))
    if CONF_DONE_LIST in config:
        cg.add(var.set_done_list(await cg.get_variable(config[CONF_DONE_LIST])))
    if CONF_SPEAKER in config:
        cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
        cg.add(var.set_alarm(smart_display.sound(config[CONF_ID], "done", DONE)))
        if CONF_SOUND_ON in config:
            cg.add(var.set_sound_on(await cg.get_variable(config[CONF_SOUND_ON])))
        if CONF_ALARM_VOLUME in config:
            cg.add(var.set_alarm_volume(await cg.get_variable(config[CONF_ALARM_VOLUME])))

    # Auto-detect voice_assistant (e.g. from voice_assist) without breaking builds that lack it.
    va_cfg = CORE.config.get("voice_assistant")
    if isinstance(va_cfg, list):
        va_cfg = va_cfg[0] if va_cfg else None
    if isinstance(va_cfg, dict) and CONF_ID in va_cfg:
        va_var = await cg.get_variable(va_cfg[CONF_ID])
        cg.add(var.set_assistant(va_var))
        for key, method in (
            ("on_timer_started", "attach_assist_started"),
            ("on_timer_updated", "attach_assist_updated"),
            ("on_timer_cancelled", "attach_assist_cancelled"),
            ("on_timer_finished", "attach_assist_finished"),
            ("on_timer_tick", "attach_assist_tick"),
        ):
            if key not in va_cfg:
                cg.add(getattr(var, method)())
