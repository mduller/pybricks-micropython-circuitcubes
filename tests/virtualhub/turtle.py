# SPDX-License-Identifier: MIT
# Copyright (c) 2022 The Pybricks Authors

import turtle
import tkinter as tk
from typing import Tuple, Union

from . import VirtualHub as BaseVirtualHub


def do_events() -> None:
    """
    Processes any pending TCL events without blocking wait.
    """
    root = turtle.getcanvas().winfo_toplevel()
    while root.dooneevent(tk._tkinter.DONT_WAIT):
        pass


def draw_hub() -> None:
    """
    Draws the non-interactive, non-animated parts of the hub.
    """
    turtle.up()
    turtle.goto(-100, -100)
    turtle.down()
    turtle.color("black")
    turtle.goto(-100, 100)
    turtle.goto(100, 100)
    turtle.goto(100, -100)
    turtle.goto(-100, -100)


def draw_light(color: Union[str, Tuple[int, int, int]]) -> None:
    """
    Draws the hub status light.

    Args:
        color: The name of a color or a tuple of RGB values.
    """
    turtle.up()
    turtle.goto(0, -75)
    turtle.down()
    turtle.color("black", color)
    turtle.begin_fill()
    turtle.circle(15)
    turtle.end_fill()


class VirtualHub(BaseVirtualHub):
    """
    This is a ``VirtualHub`` implementation that uses turtle graphics to draw
    the virtual hub.
    """

    def __init__(self) -> None:
        super().__init__()

        # we are using turtle just for drawing, so disable animations, etc.
        turtle.hideturtle()
        turtle.delay(0)
        turtle.tracer(0, 0)
        turtle.colormode(255)

        # draw initial hub
        draw_hub()
        draw_light("black")

    def on_event_poll(self) -> None:
        do_events()

    def on_light(self, id: int, r: int, g: int, b: int) -> None:
        if id != 0:
            return

        draw_light((r, g, b))