#!/usr/bin/env python3

from jinja2 import Environment, FileSystemLoader, select_autoescape
import argparse

def main():
    parser = argparse.ArgumentParser(description='Render SkyAware index html doc.')
    parser.add_argument('env', help='Name of environment to render for.(remote-dev | remote-prod | local)')

    args = parser.parse_args()

    # declare configs by environment
    configs = {
        'remote-dev': {
            'asset_path': lambda filename: '/include/LIVE-skyaware/{filename}'.format(filename=filename)
        },
        'remote-prod': {
            'asset_path': lambda filename: '/include/abcdef123456-skyaware/{filename}'.format(filename=filename)
        },
        'local': {
            'asset_path': lambda filename: '{filename}?v=4.0'.format(filename=filename)
        }
    }

    # suss out our config
    config = configs[args.env]

    asset_path = config['asset_path']

    env = Environment(
        loader=FileSystemLoader('.'),
        autoescape=select_autoescape(['html', 'xml'])
    )

    env.filters['asset_path'] = asset_path

    # render the template
    outfile = 'index.html'
    infile = outfile + '.j2'

    template = env.get_template(infile)

    params = {
        'name': 'Bob'
    }

    content = template.render(params)
    # print(content)

    with open(outfile, 'w') as fh:
       fh.write(content)


if __name__ == '__main__':
    main()