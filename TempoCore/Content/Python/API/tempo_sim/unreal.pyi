# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# The type surface of tempo_sim.unreal.
#
# This file exists for one reason, and it is the load-bearing reason behind the whole typed API:
# `Object` really does define `__getattr__` and `__setattr__` at runtime, because attribute access
# IS the API and the generated classes carry no descriptors. But a catch-all that a type checker can
# see makes *every* attribute legal on *every* generated subclass -- at which point a scenario using
# a property that a new release removed, retyped or made read-only passes silently, which is exactly
# the failure the generated stubs exist to prevent.
#
# So they are omitted here. A checker reads this file and sees a closed type; the interpreter reads
# the .py and gets the behavior.
#
# The escape hatch is `DynamicObject`, which declares them. It is a distinct type precisely so that
# reaching for it is visible at the call site and cannot leak into a typed hierarchy.

from contextlib import AbstractContextManager
from typing import Any, Callable, Generic, Iterator, TypeVar

_V = TypeVar("_V")
_T = TypeVar("_T")
_C = TypeVar("_C", covariant=True)

class Vector:
    X: float
    Y: float
    Z: float
    def __init__(self, X: float = ..., Y: float = ..., Z: float = ...) -> None: ...

class Vector2D:
    X: float
    Y: float
    def __init__(self, X: float = ..., Y: float = ...) -> None: ...

class IntVector:
    X: int
    Y: int
    Z: int
    def __init__(self, X: int = ..., Y: int = ..., Z: int = ...) -> None: ...

class IntPoint:
    X: int
    Y: int
    def __init__(self, X: int = ..., Y: int = ...) -> None: ...

class Rotator:
    Roll: float
    Pitch: float
    Yaw: float
    def __init__(self, Roll: float = ..., Pitch: float = ..., Yaw: float = ...) -> None: ...

class Quat:
    X: float
    Y: float
    Z: float
    W: float
    def __init__(self, X: float = ..., Y: float = ..., Z: float = ..., W: float = ...) -> None: ...

class Transform:
    Location: Vector
    Rotation: Rotator
    def __init__(self, Location: Vector = ..., Rotation: Rotator = ...) -> None: ...

class Color:
    R: int
    G: int
    B: int
    def __init__(self, R: int = ..., G: int = ..., B: int = ...) -> None: ...

class AssetRef(Generic[_C]):
    """A cooked asset, by path, typed by its class.

    Covariant: an AssetRef[Material] goes where an AssetRef[MaterialInterface] is wanted. Built
    from a path it is AssetRef[Unknown], which is the escape hatch for a path from data.
    """
    @property
    def path(self) -> str: ...
    @property
    def class_path(self) -> str: ...
    def __init__(self, path: str, class_path: str = ...) -> None: ...

class Struct:
    """The base of every generated struct type.

    A generated struct declares its members, and -- when it can travel as one value -- a
    keyword-only constructor naming the ones a scenario may write. Like Object, this declares no
    catch-all, so a member a struct does not have is a type error.
    """
    __tempo_struct__: str

def value_of(value: _V) -> _V:
    """A struct property's current value as a copy, rather than the live proxy a read returns."""
    ...

class Object:
    __tempo_class__: str
    def __init__(self, actor: str = ..., component: str = ..., class_path: str = ...,
                 subsystem: str = ...) -> None: ...
    @property
    def actor(self) -> str: ...
    @property
    def component(self) -> str: ...
    @property
    def subsystem(self) -> str: ...
    def is_a(self, other: type[Object] | str) -> bool: ...
    def snapshot(self, *names: str) -> AbstractContextManager[Object]: ...
    # Dynamically typed by necessity: the property is named by a string, so there is nothing for a
    # checker to narrow from.
    def watch(self, name: str) -> Iterator[Any]: ...
    # Deliberately no __getattr__ or __setattr__. See the note at the top of this file.

class DynamicObject(Object):
    """A handle with no generated type behind it.

    Returned by the schema-free lookups, where the class is not known until runtime. Any attribute
    is permitted here and checked only by the server, so nothing about it is verified before it
    runs. That is the trade, and it is a separate type so that taking it is a visible choice.
    """
    def __getattr__(self, name: str) -> Any: ...
    def __setattr__(self, name: str, value: Any) -> None: ...

class StructProxy:
    def __getattr__(self, name: str) -> Any: ...
    def __setattr__(self, name: str, value: Any) -> None: ...

class BatchError(RuntimeError):
    index: int
    code: int
    error: str

class Deferred(Generic[_T]):
    """A read or call queued in a batch, available once the batch has run."""
    @property
    def done(self) -> bool: ...
    @property
    def value(self) -> _T: ...

class Batch:
    def __enter__(self) -> Batch: ...
    # None, not bool: a batch never swallows the block's exception, and a checker reading `bool`
    # would treat everything assigned inside the block as possibly unbound after it.
    def __exit__(self, *exc: Any) -> None: ...
    def get(self, read: Callable[[], _T]) -> Deferred[_T]: ...
    def call(self, invoke: Callable[[], _T]) -> Deferred[_T]: ...
    def flush(self) -> None: ...

def register_classes(package: str, directory: str) -> None: ...
def registered_classes() -> Any: ...
