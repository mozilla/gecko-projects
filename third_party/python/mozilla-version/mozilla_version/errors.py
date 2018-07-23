
class InvalidVersionError(ValueError):
    def __init__(self, version_string):
        super(InvalidVersionError, self).__init__(
            'Version "{}" does not match the pattern of a valid version'.format(version_string)
        )


class NoVersionTypeError(ValueError):
    def __init__(self, version_string):
        super(NoVersionTypeError, self).__init__(
            'Version "{}" matched the pattern of a valid version, but it is unable to find what type it is. \
This is likely a bug in mozilla-version'.format(version_string)
        )


class MissingFieldError(ValueError):
    def __init__(self, version_string, field_name):
        super(MissingFieldError, self).__init__(
            'Release "{}" does not contain a valid {}'.format(version_string, field_name)
        )


class TooManyTypesError(ValueError):
    def __init__(self, version_string, first_matched_type, second_matched_type):
        super(TooManyTypesError, self).__init__(
            'Release "{}" cannot match types "{}" and "{}"'.format(
                version_string, first_matched_type, second_matched_type
            )
        )
