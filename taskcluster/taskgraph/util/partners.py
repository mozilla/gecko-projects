# TODO - redo errors to keep in style ??

from __future__ import absolute_import, print_function, unicode_literals

import logging

log = logging.getLogger(__name__)

partner_configs = {}


def check_if_partners_enabled(config, tasks):
    if (
        config.params['release_enable_partners'] and
        config.kind.startswith('release-partner-repack')
    ) or (
        config.params['release_enable_emefree'] and
        config.kind.startswith('release-eme-free-repack')
    ):
        for task in tasks:
            yield task


def get_partner_config_by_kind(config, kind):
    """ Retrieve partner data starting from the manifest url, which points to a repository
    containing a default.xml that is intended to be drive the Google tool 'repo'. It
    descends into each partner repo to lookup and parse the repack.cfg file(s).

    Supports caching data by kind to avoid repeated requests, relying on the related kinds for
    partner repacking, signing, repackage, repackage signing all having the same kind prefix.
    """
    partner_subset = config.params['release_partners']
    partner_configs = config.params['release_partner_config'] or {}

    # TODO eme-free should be a partner; we shouldn't care about per-kind
    for k in partner_configs:
        if kind.startswith(k):
            kind_config = partner_configs[k]
            break
    else:
        return {}
    # if we're only interested in a subset of partners we remove the rest
    if isinstance(partner_subset, (list, tuple)):
        # TODO - should be fatal to have an unknown partner in partner_subset
        for partner in kind_config.keys():
            if partner not in partner_subset:
                del(kind_config[partner])

    return kind_config
