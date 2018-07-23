import os
from setuptools import setup, find_packages


project_dir = os.path.abspath(os.path.dirname(__file__))

with open(os.path.join(project_dir, 'version.txt')) as f:
    version = f.read().rstrip()

# We allow commented lines in this file
with open(os.path.join(project_dir, 'requirements.txt')) as f:
    requirements = [line.rstrip('\n') for line in f if not line.startswith('#')]


setup(
    name='mozilla-version',
    version=version,
    description="""Process Firefox versions numbers. Tells whether they are valid or not, whether \
they are nightlies or regular releases, whether this version precedes that other.
    """,
    author='Mozilla Release Engineering',
    author_email='release+python@mozilla.com',
    url='https://github.com/mozilla-releng/mozilla-version',
    packages=find_packages(),
    include_package_data=True,
    zip_safe=False,
    license='MPL2',
    install_requires=requirements,
    classifiers=(
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
    ),
)
