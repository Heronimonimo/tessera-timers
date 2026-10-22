"""Timers: a kitchen timer and a stopwatch on the glass.

The timers and stopwatches that run on the screen live in the component (timers.cpp), one slot per tile of the layout,
and are kept in the screen's flash so a restart picks them up where they were. A timer tile that belongs to a timer
helper of Home Assistant starts, pauses and cancels that one instead. face.cpp lays out and paints the ring, the time
and the keys for every size of tile and for the cards. register_plugin() takes the id, version, tile memory and the
screen's texts from the manifest and translations/ beside this folder.

On a screen with a speaker the part parts/sound.yaml hands it over as `speaker`: a timer that is done then rings with
sounds/done.wav, built in by smart_display.sound() and played by the core's sound player (plugin_sound.h).
"""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, smart_display, speaker, switch
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "binary_sensor", "switch"]

CONF_DONE = "done"
CONF_OPENS = "opens_when_done"
CONF_SPEAKER = "speaker"
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
            cv.Required(CONF_OPENS): cv.use_id(switch.Switch),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    check_sound,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_done(await cg.get_variable(config[CONF_DONE])))
    cg.add(var.set_opens(await cg.get_variable(config[CONF_OPENS])))
    if CONF_SPEAKER in config:
        cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
        cg.add(var.set_alarm(smart_display.sound(config[CONF_ID], "done", DONE)))
