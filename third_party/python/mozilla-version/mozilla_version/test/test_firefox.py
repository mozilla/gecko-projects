import pytest
import re

import mozilla_version.firefox

from mozilla_version.errors import InvalidVersionError, TooManyTypesError, NoVersionTypeError
from mozilla_version.firefox import FirefoxVersion


VALID_VERSIONS = {
    '32.0a1': 'nightly',
    '32.0a2': 'aurora_or_devedition',
    '32.0b2': 'beta',
    '32.0b10': 'beta',
    '32.0': 'release',
    '32.0.1': 'release',
    '32.0esr': 'esr',
    '32.0.1esr': 'esr',
}


@pytest.mark.parametrize('version_string', (
    '32', '32.b2', '.1', '32.2', '32.02', '32.0a1a2', '32.0a1b2', '32.0b2esr', '32.0esrb2',
))
def test_firefox_version_raises_when_invalid_version_is_given(version_string):
    with pytest.raises(InvalidVersionError):
        FirefoxVersion(version_string)


@pytest.mark.parametrize('version_string, expected_type', VALID_VERSIONS.items())
def test_firefox_version_is_of_a_defined_type(version_string, expected_type):
    release = FirefoxVersion(version_string)
    assert getattr(release, 'is_{}'.format(expected_type))


@pytest.mark.parametrize('previous, next', (
    ('32.0', '33.0'),
    ('32.0', '32.1.0'),
    ('32.0', '32.0.1'),
    ('32.0build1', '32.0build2'),

    ('32.0a1', '32.0'),
    ('32.0a2', '32.0'),
    ('32.0b1', '32.0'),

    ('32.0.1', '33.0'),
    ('32.0.1', '32.1.0'),
    ('32.0.1', '32.0.2'),
    ('32.0.1build1', '32.0.1build2'),

    ('32.1.0', '33.0'),
    ('32.1.0', '32.2.0'),
    ('32.1.0', '32.1.1'),
    ('32.1.0build1', '32.1.0build2'),

    ('32.0b1', '33.0b1'),
    ('32.0b1', '32.0b2'),
    ('32.0b1build1', '32.0b1build2'),

    ('2.0', '10.0'),
    ('10.2.0', '10.10.0'),
    ('10.0.2', '10.0.10'),
    ('10.10.1', '10.10.10'),
    ('10.0build2', '10.0build10'),
    ('10.0b2', '10.0b10'),
))
def test_firefox_version_implements_lt_operator(previous, next):
    assert FirefoxVersion(previous) < FirefoxVersion(next)


@pytest.mark.parametrize('equivalent_version_string', (
    '32.0', '032.0', '32.0build1', '32.0build01', '32.0esr',
))
def test_firefox_version_implements_eq_operator(equivalent_version_string):
    assert FirefoxVersion('32.0') == FirefoxVersion(equivalent_version_string)


def test_firefox_version_implements_remaining_comparision_operators():
    assert FirefoxVersion('32.0') <= FirefoxVersion('32.0')
    assert FirefoxVersion('32.0') <= FirefoxVersion('33.0')

    assert FirefoxVersion('33.0') >= FirefoxVersion('32.0')
    assert FirefoxVersion('33.0') >= FirefoxVersion('33.0')

    assert FirefoxVersion('33.0') > FirefoxVersion('32.0')
    assert not FirefoxVersion('33.0') > FirefoxVersion('33.0')

    assert not FirefoxVersion('32.0') < FirefoxVersion('32.0')

    assert FirefoxVersion('33.0') != FirefoxVersion('32.0')


@pytest.mark.parametrize('version_string', (
    '32.0', '032.0', '32.0build1', '32.0build01',
))
def test_firefox_version_implements_str_operator(version_string):
    assert str(FirefoxVersion(version_string)) == version_string


_SUPER_PERMISSIVE_PATTERN = re.compile(r"""
(?P<major_number>\d+)\.(?P<zero_minor_number>\d+)(\.(\d+))*
(?P<is_nightly>a1)?(?P<is_aurora_or_devedition>a2)?(b(?P<beta_number>\d+))?
(?P<is_two_digit_esr>esr)?(?P<is_three_digit_esr>esr)?
""", re.VERBOSE)


@pytest.mark.parametrize('version_string', (
    '32.0a1a2', '32.0a1b2', '32.0b2esr'
))
def test_firefox_version_ensures_it_does_not_have_multiple_type(monkeypatch, version_string):
    # Let's make sure the sanity checks detect a broken regular expression
    monkeypatch.setattr(
        mozilla_version.firefox, '_VALID_VERSION_PATTERN', _SUPER_PERMISSIVE_PATTERN
    )

    with pytest.raises(TooManyTypesError):
        FirefoxVersion(version_string)


def test_firefox_version_ensures_a_new_added_release_type_is_caught(monkeypatch):
    # Let's make sure the sanity checks detect a broken regular expression
    monkeypatch.setattr(
        mozilla_version.firefox, '_VALID_VERSION_PATTERN', _SUPER_PERMISSIVE_PATTERN
    )
    # And a broken type detection
    FirefoxVersion.is_release = False

    with pytest.raises(NoVersionTypeError):
        mozilla_version.firefox.FirefoxVersion('32.0.0.0')
