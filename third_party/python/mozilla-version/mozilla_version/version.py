from enum import Enum


class VersionType(Enum):
    NIGHTLY = 1
    AURORA_OR_DEVEDITION = 2
    BETA = 3
    RELEASE = 4
    # ESR has the same value than RELEASE because 60.0.1 is the same codebase
    # than 60.0.1esr, for instance
    ESR = 4

    def __eq__(self, other):
        return self.compare(other) == 0

    def __ne__(self, other):
        return self.compare(other) != 0

    def __lt__(self, other):
        return self.compare(other) < 0

    def __le__(self, other):
        return self.compare(other) <= 0

    def __gt__(self, other):
        return self.compare(other) > 0

    def __ge__(self, other):
        return self.compare(other) >= 0

    def compare(self, other):
        return self.value - other.value
