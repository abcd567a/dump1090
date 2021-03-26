#!/usr/bin/env python3

from jinja2 import Environment, FileSystemLoader, select_autoescape
import argparse

def main():
    parser = argparse.ArgumentParser(description='Render SkyAware index html doc.')
    parser.add_argument('env', help='Name of environment to render for.(remote-dev | remote-prod | local)')

    args = parser.parse_args()

    # declare configs by environment
    configs = {
        'remote': {
            'outfile': 'index.rvt',
            'filters': {
                'asset_path': lambda filename: '<?= [skyaware_include_resource_filename {filename}] ?>'.format(filename=filename)
            },
            'params': {
                'preface': '<? skyaware_init ?>'
            }
        },
        'local': {
            'outfile': 'index.html',
            'filters': {
                'asset_path': lambda filename: '{filename}?v=4.0'.format(filename=filename)
            },
            'params': {
                'preface': ''
            }
        }
    }

    # suss out our config
    config = configs[args.env]

    env = Environment(
        loader=FileSystemLoader('.'),
        autoescape=select_autoescape(['html', 'xml'])
    )

    env.filters = config['filters']
    params = config['params']

    # render the template
    outfile = config['outfile']
    infile = 'index.j2'

    template = env.get_template(infile)

    content = template.render(params)

    with open(outfile, 'w') as fh:
       fh.write(content)


if __name__ == '__main__':
    main()