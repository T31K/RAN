"""Paths + config shared by the custom mob scripts (no numpy here, so install.py runs anywhere)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'port', 'build', 'custom_mobs')     # build outputs (git-ignored)
MODELS = os.environ.get('RAN_MOB_MODELS', os.path.join(REPO, 'assets', 'custom-mobs'))


def load_config():
    return json.load(open(os.path.join(HERE, 'mobs.json')))


def mob_keys(cfg, wanted=()):
    keys = list(wanted) or [k for k in cfg if not k.startswith('_')]
    for k in keys:
        assert k in cfg, f'unknown mob {k!r}; have {[k for k in cfg if not k.startswith("_")]}'
    return keys


def built_dir(cfg, key):
    return os.path.join(OUT, 'mobs', cfg[key]['tag'])


def load_built(cfg, key):
    path = os.path.join(built_dir(cfg, key), 'crow.json')
    assert os.path.exists(path), f'{key}: run make_mob.py {key} first'
    return json.load(open(path))
