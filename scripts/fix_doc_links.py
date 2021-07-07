#!/usr/bin/env python3

import os
import sys
import time
import argparse
import re
from bs4 import BeautifulSoup
from multiprocessing import Pool
from functools import partial

# Extract all hexadecimal characters at the end of a string.
HASH_SEARCH_TERM = r'([0-9A-Fa-f]*)$'
IS_HEXADECIMAL = r'^[0-9A-fa-f]*$'
# Bad links will have an underscore and 3 hexadecimal characters before the extension.
BAD_LINK_SEARCH_TERM = r'_[0-9A-Fa-f]{3}\.html'
HTML_FILE_SEARCH_TERM = r'\.html$'
JS_FILE_SEARCH_TERM = r'\.js$'
LINK_SEARCH_TERM = r'"(\S*?\.html.*?)"'
link_whitelist = []

class HtmlFile:
    """A class of files with a .html extension"""

    def __init__( self, html_file_name ):
        self.ids = []
        self.links = []
        self.name = html_file_name
        self.abspath = os.path.abspath( html_file_name )
        self.broken_links = []
        self.fixed_links_list = []
        self.external = False
        with open( html_file_name, 'r' ) as infile:
            html_data = infile.read()
        dirname = os.path.dirname( self.name )
        soup = BeautifulSoup( html_data, 'html.parser' )
        for tag in soup.find_all( True, { 'id': True } ):
            self.ids.append( tag.get( 'id' ) )
        for tag in soup.find_all( 'a' ):
            link = tag.get( 'href' )
            if link is not None and 'http' not in link and link not in self.links:
                self.links.append( link )

    def id_exists(self, id):
        return id in self.ids

    def find_broken_links( self, files, whitelist ):
        dirname = os.path.dirname( self.name )
        for link in self.links:
            if link in whitelist:
                continue
            link_elements = link.split( '#' )
            path = link_elements[ 0 ]
            id = None
            if len( link_elements ) > 1:
                id = link_elements[ 1 ]
            if path == '':
                if id is not None:
                    matched_hex = re.search( IS_HEXADECIMAL, id )
                    if matched_hex is None:
                        continue
                if id not in self.ids:
                    self.broken_links.append( link )
                continue
            filename = os.path.join( dirname, path )
            absfile = os.path.abspath( filename )
            if absfile not in files:
                self.broken_links.append( link )
                continue
            file_obj = files[ absfile ]
            if file_obj is None:
                self.broken_links.append( link )
            elif id is None:
                continue
            else:
                matched_hex = re.search( IS_HEXADECIMAL, id )
                if matched_hex is None:
                    continue
                if not file_obj.id_exists( id ):
                    self.broken_links.append( link )

    def find_fixed_links( self ):
        if len( self.broken_links ) == 0:
            return
        with open( self.name, 'r' ) as infile:
            html_data = infile.read()
        soup = BeautifulSoup( html_data, 'html.parser' )
        table_rows = soup.find_all( 'tr' )
        link_map = {}
        for row in table_rows:
            try:
                row_class_name = row[ 'class' ]
            except KeyError as e:
                #Skip if no class tag.
                continue
            if row_class_name is not None:
                # Extract the hexadecimal hash used in the class of each table row.
                matched_hash_object = re.search( HASH_SEARCH_TERM, row_class_name[ 0 ] )
                if matched_hash_object is not None:
                    hash = matched_hash_object.group( 1 )
                else:
                    continue
            else:
                continue
            # Get links
            links = row.find_all( 'a', attrs={ 'href': True } )
            for elem in links:
                link_class_name = elem.get( 'class' )
                link = elem.get( 'href' )
                # Ignore links to data types as we are only interested in variables.
                # This filter is based on manual observation.
                if link_class_name is not None and link_class_name[ 0 ] == 'elRef':
                    continue
                # The broken and correct links will be in the same table row, so index them by the hash found in the row.
                if hash in link_map:
                    if link_map[ hash ] != link:
                        if link in self.broken_links:
                            self.fixed_links_list.append( ( link, link_map[ hash ] ) )
                        else:
                            # Update the link as it's probably correct if it's closer.
                            link_map[ hash ] = link
                else:
                    link_map[ hash ] = link

    def replace_links( self ):
        if len( self.fixed_links_list ) == 0:
            return
        with open( self.name, 'r' ) as infile:
            html_data = infile.read()
        for broken_link, fixed_link in self.fixed_links_list:
            html_data = html_data.replace( broken_link, fixed_link )
        with open( self.name, 'w' ) as outfile:
            outfile.write( html_data )


def test_links( html_file ):
    """Tests all hrefs in a file for existence in filesystem.

    Args:
        html_file: Name of file to test

    Returns:
        True if all links are valid, False otherwise
    """

    ret_val = True
    with open( html_file, 'r' ) as infile:
        html_data = infile.read()
    dirname = os.path.dirname( html_file )
    soup = BeautifulSoup( html_data, 'html.parser' )
    links = soup.find_all( 'a' )
    for elem in links:
        link = elem.get( 'href' )
        if link is not None:
            substrings = link.split( '#' )
            if substrings[ 0 ] == '' or 'http' in substrings[ 0 ]:
                continue
            filename = os.path.join( dirname, substrings[ 0 ] )
            if not os.path.isfile( filename ):
                print( 'Broken: ' + link + ' in file ' + html_file )
                ret_val = False
    return ret_val

def get_fixed_links( html_data ):
    """Get fixed links.

    Uses regex to identify links in table rows that are broken, and the link
    in the same row that is likely to be the correct one. Both of these
    identifications is based on manual observation of the doxygen output,
    and may need to be changed in the future.

    Args:
        html_data: Content of an HTML file

    Returns:
        Dict mapping broken links to their fixed versions
    """

    soup = BeautifulSoup( html_data, 'html.parser' )
    table_rows = soup.find_all( 'tr' )
    link_map = {}
    fixed_links_list = []
    for row in table_rows:
        try:
            row_class_name = row[ 'class' ]
        except KeyError as e:
            #Skip if no class tag.
            continue
        if row_class_name is not None:
            # Extract the hexadecimal hash used in the class of each table row.
            matched_hash_object = re.search( HASH_SEARCH_TERM, row_class_name[ 0 ] )
            if matched_hash_object is not None:
                hash = matched_hash_object.group( 1 )
            else:
                continue
        else:
            continue
        # Get links
        links = row.find_all( 'a' )
        for elem in links:
            link_class_name = elem.get( 'class' )
            link = elem.get( 'href' )
            if link is None:
                continue
            # Ignore links to data types as we are only interested in variables.
            # This filter is based on manual observation.
            if link_class_name is not None and link_class_name[ 0 ] == 'elRef':
                continue
            # The broken and correct links will be in the same table row, so index them by the hash found in the row.
            if hash in link_map:
                if link_map[ hash ] != link:
                    is_bad_link = re.search( BAD_LINK_SEARCH_TERM, link )
                    if is_bad_link is not None:
                        fixed_links_list.append( ( link, link_map[ hash ] ) )
                    else:
                        # Update the link as it's probably correct if it's closer.
                        link_map[ hash ] = link
            else:
                link_map[ hash ] = link
    return fixed_links_list

def print_links( links ):
    """Print broken links and their fixed versions.

    Args:
        links: Dict of fixed links indexed by broken versions.
    """

    for broken_link, fixed_link in links:
        print( '\tBroken: ' + broken_link )
        print( '\tFixed: ' + fixed_link )

def replace_links( html_data, links ):
    """Replaces links in a segment of text

    Args:
        html_data: Content to be replaced
        links: Dict of fixed links indexed by broken versions.
    """

    for broken_link, fixed_link in links:
        html_data = html_data.replace( broken_link, fixed_link )
    return html_data

def process_file( html_file, flags ):
    """Processes a file, either testing all links or replacing broken ones.

    Args:
        html_file: Name of file
        flags: Flags to change behavior

    Returns:
        False if broken link found (when testing its links), else True
    """

    if flags[ 'fix_links' ]:
        with open( html_file, 'r' ) as infile:
            html_data = infile.read()
        fixed_links = get_fixed_links( html_data )
        if len( fixed_links ) > 0:
            if flags[ 'verbosity' ] or flags[ 'dry_run' ]:
                print( 'FILE: ' + html_file )
                print_links( fixed_links )
            html_data = replace_links( html_data, fixed_links )
            if not flags[ 'dry_run' ]:
                with open( html_file, 'w' ) as outfile:
                    outfile.write( html_data )
        # Return success
        return True
    else:
        return test_links( html_file )

def find_external( files, first ):
    if first == '':
        return
    queue = [ first ]
    files[first].external = True
    while len( queue ) > 0:
        cur_file = queue[ 0 ]
        queue.pop( 0 )
        dirname = os.path.dirname(cur_file)
        for link in files[cur_file].links:
            link_elements = link.split( '#' )
            path = link_elements[ 0 ]
            if path == '':
                continue
            path = os.path.join(dirname, path)
            path = os.path.abspath(path)
            if os.path.exists(path):
                temp_file = files[path]
                if temp_file is not None:
                    if not temp_file.external:
                        queue.append(path)
                        temp_file.external = True


def main():
    global link_whitelist
    parser = argparse.ArgumentParser(
        description='A script to identify broken links. By default, tests all links for existence.',
        epilog='Requires beautifulsoup4'
    )
    parser.add_argument( "-F", "--files", action="store", dest="files", nargs='+', help="HTML files to fix" )
    parser.add_argument( "directory", action="store", nargs='?', help="Doxygen output directory" )
    parser.add_argument( "-f", "--fix-links", action="store_true", default=False, help="Identify fixed links" )
    parser.add_argument( "-v", "--verbose", action="store_true", default=False, help="Print broken and fixed links. Used with -f" )
    parser.add_argument( "-d", "--dry-run", action="store_true", default=False, help="Don't overwrite existing files when identifying fixed links. Used with -f" )
    parser.add_argument( "-n", "--num-processes", action="store", type=int, default=4, help="Number of processes to run in parallel" )
    args = parser.parse_args()
    file_list = []
    js_files = []
    index_file = ''
    if args.files is not None:
        file_list = args.files
    elif args.directory is not None:
        for root_path, directories, files in os.walk( args.directory ):
            for filename in files:
                # We only want HTML files.
                full_name = os.path.join( root_path, filename )
                if re.search( HTML_FILE_SEARCH_TERM, filename ):
                    file_list.append( full_name )
                elif re.search( JS_FILE_SEARCH_TERM, filename ):
                    js_files.append( full_name )
                if 'main/index.html' in full_name:
                    index_file = os.path.abspath( full_name )
    else:
        parser.error( 'Either directory or files must be provided.' )
    flags = { 'verbosity': args.verbose, 'dry_run': args.dry_run, 'fix_links': args.fix_links }
    for f in js_files:
        with open( f, 'r' ) as infile:
            for line in infile:
                is_link = re.search( LINK_SEARCH_TERM, line )
                if is_link:
                    link_whitelist.append( is_link.group( 1 ) )
    print(len(link_whitelist))
    # Process files in parallel.
    files = {}
    for f in file_list:
        new_file = HtmlFile( f )
        files[ new_file.abspath ] = new_file
    print(len(files))
    find_external( files, index_file )
    external_count = 0
    internal_count = 0
    for path, file_obj in files.items():
        if file_obj.external:
            external_count += 1
            file_obj.find_broken_links( files, link_whitelist )
            file_obj.find_fixed_links()
            for link in file_obj.broken_links:
                print('Broken link: ' + link + ' in file ' + path )
            if len( file_obj.fixed_links_list ) > 0:
                print_links( file_obj.fixed_links_list )
                file_obj.replace_links()
        else:
            internal_count += 1
    print(external_count)
    print(internal_count)

    # pool = Pool( args.num_processes )
    # return_values = pool.map( partial( process_file, flags=flags ), file_list )
    # pool.close()
    # pool.join()
    # if all( return_values ):
    #     sys.exit( 0 )
    # sys.exit( 1 )

if __name__ == "__main__":
    main()

