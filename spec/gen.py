#!/usr/bin/env python

import argparse
import jinja2
import re
import sys
import yaml

split_re = re.compile(r'^(?P<type>.*?)\s*(?P<name>\w+)$')
env = jinja2.Environment(
    trim_blocks=True,
    lstrip_blocks=True,
    loader=jinja2.FileSystemLoader('template'),
)

def args(args, add_type=True, prefix=''):
    return ', '.join(
        '{} {}{}'.format(arg['type'], prefix, arg['name']) if add_type else prefix + arg['name']
        for arg in args
    )

printf_lookup = {
    'GLbitfield':    'd',
    'GLboolean':     'd',
    'GLbyte':        'c',
    'GLchar':        'c',
    'GLclampd':      '0.2f',
    'GLclampf':      '0.2f',
    'GLclampx':      'd',
    'GLdouble':      '0.2f',
    'GLenum':        '0x%04X',
    'GLfixed':       'd',
    'GLfloat':       '0.2f',
    'GLhalfNV':      'd',
    'GLint':         'd',
    'GLint64EXT':    '"PRIi64"',
    'GLintptr':      'td',
    'GLintptrARB':   'td',
    'GLhandleARB':   'u',
    'GLshort':       'd',
    'GLsizei':       'd',
    'GLsizeiptr':    'td',
    'GLsizeiptrARB': 'td',
    'GLubyte':       'c',
    'GLuint':        'u',
    'GLuint64':      '"PRIu64"',
    'GLuint64EXT':   '"PRIu64"',
    'GLushort':      'u',
    'GLvoid':        'p',
    'GLvdpauSurfaceNV': 'td',

    'bool':      'd',
    'double':    'lf',
    'float':     'f',
    'int':       'd',
    'long long': 'll',
    'long':      'l',
    'unsigned int':       'u',
    'unsigned long long': 'llu',
    'unsigned long':      'lu',

    'int8_t':   '"PRIi8"',
    'int16_t':  '"PRIi16"',
    'int32_t':  '"PRIi32"',
    'int64_t':  '"PRIi64"',
    'uint8_t':  '"PRIu8"',
    'uint16_t': '"PRIu16"',
    'uint32_t': '"PRIu32"',
    'uint64_t': '"PRIu64"',

    'Bool': 'd',
    'Colormap': 'lu',
    'Font': 'lu',
    'GLXDrawable': 'd',
    'Pixmap': 'lu',
    'Window': 'lu',
}

def printf(args):
    if isinstance(args, dict):
        args = (args,)

    types = []
    for arg in args:
        typ = arg['type']
        if '*' in typ:
            t = 'p'
        else:
            t = printf_lookup.get(typ, 'p')
        if not '%' in t:
            t = '%' + t
        types.append(t)

    return ', '.join(types)

def unconst(s):
    split = s.split(' ')
    while 'const' in split:
        split.remove('const')
    return ' '.join(split)

env.filters['args'] = args
env.filters['printf'] = printf
env.filters['unconst'] = unconst

def split_arg(arg):
    match = split_re.match(arg)
    if match:
        return match.groupdict()
    else:
        return {'type': 'unknown', 'name': arg}

def gen(files, template, guard_name, headers,
        deep=False, cats=(), ifdef=None, ifndef=None, skip=None):
    funcs = {}
    formats = []
    unique_formats = set()
    for data in files:
        if deep and not isinstance(data.values()[0], list):
            functions = []
            for cat, f in data.items():
                if not cats or cat in cats:
                    functions.extend(f.items())
        else:
            functions = data.items()

        functions = [f for f in functions if not skip or not f[0] in skip]

        for name, args in sorted(functions):
            props = {}
            if args:
                ret = args.pop(0)
            else:
                ret = 'void'

            args = [split_arg(arg) for arg in args if not arg == 'void']
            if any(arg.get('type') == 'unknown' for arg in args):
                continue

            if args:
                args[0]['first'] = True
                args[-1]['last'] = True

            for i, arg in enumerate(args):
                arg['index'] = i

            types = '_'.join(
                arg['type'].replace(' ', '_').replace('*', '__GENPT__')
                for arg in [{'type': ret}] + args)

            props.update({
                'return': ret,
                'name': name,
                'args': args,
                'types': types,
                'void': ret == 'void',
            })
            if not types in unique_formats:
                unique_formats.add(types)
                formats.append(props)

            funcs[name] = props

    context = {
        'functions': [i[1] for i in sorted(funcs.items())],
        'formats': formats,
        'headers': headers,
        'name': guard_name,
        'ifdef': ifdef,
        'ifndef': ifndef,
    }

    t = env.get_template(template)
    return t.render(**context).rstrip('\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Generate code with yml/jinja.')
    parser.add_argument('yaml', help='spec files')
    parser.add_argument('template', help='jinja template to load')
    parser.add_argument('name', help='header guard name')
    parser.add_argument('headers', nargs='*', help='headers to include')
    parser.add_argument('--deep', help='nested definitions', action='store_true')
    parser.add_argument('--cats', help='deep category filter')
    parser.add_argument('--ifdef', help='wrap with ifdef')
    parser.add_argument('--ifndef', help='wrap with ifndef')
    parser.add_argument('--skip', help='skip function from yml')

    args = parser.parse_args()

    files = []
    for name in args.yaml.split(','):
        with open(name) as f:
            data = yaml.load(f)
            if data:
                files.append(data)

    skip = None
    if args.skip:
        with open(args.skip) as f:
            skip = yaml.load(f)

    if args.cats:
        cats = args.cats.split(',')
    else:
        cats = None
    print gen(files, args.template, args.name,
              args.headers, args.deep, cats,
              args.ifdef, args.ifndef, skip=skip)
