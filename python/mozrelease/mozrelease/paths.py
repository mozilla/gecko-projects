from urlparse import urlunsplit

from .platforms import ftp2buildhub

product_ftp_map = {
    "fennec": "mobile",
}

def product2ftp(product):
    return product_ftp_map.get(product, product)


def getCandidatesDir(product, version, buildNumber, protocol=None, server=None):
    if protocol:
        assert server is not None, "server is required with protocol"

    product = product2ftp(product)
    directory = "/{}/candidates/{}-candidates/build{}".format(
        product, str(version), str(buildNumber)
    )

    if protocol:
        return urlunsplit((protocol, server, directory, None, None))
    else:
        return directory


def getReleasesDir(product, version=None, protocol=None, server=None):
    if protocol:
        assert server is not None, "server is required with protocol"

    directory = "/{}/releases".format(product)
    if version:
        directory = "{}/{}".format(directory, version)

    if protocol:
        return urlunsplit((protocol, server, directory, None, None))
    else:
        return directory


def getReleaseInstallerPath(productName, brandName, version, platform,
                            locale='en-US'):
    if productName not in ('fennec',):
        if platform.startswith('linux'):
            filename = '%s.tar.bz2' % productName
            return '/'.join([p.strip('/') for p in [
                platform, locale, '%s-%s.tar.bz2' % (productName, version)]])
        elif platform.startswith('macosx'):
            filename = '%s.dmg' % productName
            return '/'.join([p.strip('/') for p in [
                platform, locale, '%s %s.dmg' % (brandName, version)]])
        elif platform.startswith('win'):
            filename = '%s.zip' % productName
            instname = '%s.exe' % productName
            prefix = []
            prefix.extend([platform, locale])
            return '/'.join(
                [p.strip('/') for p in
                 prefix + ['%s Setup %s.exe' % (brandName, version)]]
            )
        else:
            raise "Unsupported platform"
    else:
        if platform.startswith('android'):
            filename = '%s-%s.%s.android-arm.apk' % (
                productName, version, locale)
            prefix = []
            prefix.extend([platform, locale])
            return '/'.join(
                [p.strip('/') for p in
                 prefix + [filename]]
            )
        elif platform == 'linux':
            filename = '%s.tar.bz2' % productName
            return '/'.join([p.strip('/') for p in [
                platform, locale, '%s-%s.%s.linux-i686.tar.bz2' % (productName, version, locale)]])
        elif platform == 'macosx':
            filename = '%s.dmg' % productName
            return '/'.join([p.strip('/') for p in [
                platform, locale, '%s-%s.%s.mac.dmg' % (brandName, version, locale)]])
        elif platform == 'win32':
            filename = '%s.zip' % productName
            return '/'.join([p.strip('/') for p in [
                platform, locale,
                '%s-%s.%s.win32.zip' % (productName, version, locale)]])
        else:
            raise "Unsupported platform"


def getBuildhubReleaseUrl(api_root, product, version, platform):
    # examples:
    # firefox_beta_58-0b16_win64_en-us
    # firefox_58-0_win64_en-us
    # firefox_52-6-0esr_win64_en-us
    # devedition_aurora_59-0b3_win64_en-us
    if "b" in version:
        if product == "firefox":
            product += "_beta"
        if product == "devedition":
            product += "_aurora"
    return "{}/collections/releases/records/{}_{}_{}_en-us".format(
        api_root, product, version.replace(".", "-"),
        ftp2buildhub(platform)
    )
