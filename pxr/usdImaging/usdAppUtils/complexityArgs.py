#
# Copyright 2019 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

class RefinementComplexities(object):
    """
    An enum-like container of standard complexity settings.
    """

    class _RefinementComplexity(object):
        """
        Class which represents a level of mesh refinement complexity. Each
        level has a string identifier, a display name, and a float complexity
        value.
        """

        def __init__(self, compId, name, value):
            self._id = compId
            self._name = name
            self._value = value

        def __repr__(self):
            return self.id

        @property
        def id(self):
            return self._id

        @property
        def name(self):
            return self._name

        @property
        def value(self):
            return self._value

    LOW = _RefinementComplexity("low", "Low", 1.0)
    MEDIUM = _RefinementComplexity("medium", "Medium", 1.1)
    HIGH = _RefinementComplexity("high", "High", 1.2)
    VERY_HIGH = _RefinementComplexity("veryhigh", "Very High", 1.3)

    _ordered = (LOW, MEDIUM, HIGH, VERY_HIGH)

    @classmethod
    def ordered(cls):
        """
        Get a tuple of all complexity levels in order.
        """
        return cls._ordered

    @classmethod
    def fromId(cls, compId):
        """
        Get a complexity from its identifier.
        """
        matches = [comp for comp in cls._ordered if comp.id == compId]
        if len(matches) == 0:
            raise ValueError("No complexity with id '{}'".format(compId))
        return matches[0]

    @classmethod
    def fromName(cls, name):
        """
        Get a complexity from its display name.
        """
        matches = [comp for comp in cls._ordered if comp.name == name]
        if len(matches) == 0:
            raise ValueError("No complexity with name '{}'".format(name))
        return matches[0]

    @classmethod
    def next(cls, comp):
        """
        Get the next highest level of complexity. If already at the highest
        level, return it.
        """
        if comp not in cls._ordered:
            raise ValueError("Invalid complexity: {}".format(comp))
        nextIndex = min(
            len(cls._ordered) - 1,
            cls._ordered.index(comp) + 1)
        return cls._ordered[nextIndex]

    @classmethod
    def prev(cls, comp):
        """
        Get the next lowest level of complexity. If already at the lowest
        level, return it.
        """
        if comp not in cls._ordered:
            raise ValueError("Invalid complexity: {}".format(comp))
        prevIndex = max(0, cls._ordered.index(comp) - 1)
        return cls._ordered[prevIndex]


def AddCmdlineArgs(argsParser, defaultValue=RefinementComplexities.LOW,
        altHelpText=''):
    """
    Adds complexity-related command line arguments to argsParser.

    The resulting 'complexity' argument will be one of the standard
    RefinementComplexities.
    """
    helpText = altHelpText
    if not helpText:
        helpText = ('level of refinement to use (default=%(default)s)')

    argsParser.add_argument('--complexity', '-c', action='store',
        type=RefinementComplexities.fromId,
        default=defaultValue,
        choices=[c for c in RefinementComplexities.ordered()],
        help=helpText)
