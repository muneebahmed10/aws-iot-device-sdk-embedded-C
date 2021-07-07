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
# Bad links will have an underscore and 3 hexadecimal characters before the extension.
BAD_LINK_SEARCH_TERM = r'_[0-9A-Fa-f]{3}\.html'
HTML_FILE_SEARCH_TERM = r'\.html$'

def test_links( html_file ):
    """Tests all hrefs in a file for existence in filesystem.

    Args:
        html_file: Name of file to test

    Returns:
        True if all links are valid, False otherwise
    """

    ret_val = True
    print('Processing file: ' + html_file )
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

def main():
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
    if args.files is not None:
        file_list = args.files
    elif args.directory is not None:
        for root_path, directories, files in os.walk( args.directory ):
            for filename in files:
                # We only want HTML files.
                if re.search( HTML_FILE_SEARCH_TERM, filename ):
                    file_list.append( os.path.join (root_path, filename ) )
    else:
        parser.error( 'Either directory or files must be provided.' )
    flags = { 'verbosity': args.verbose, 'dry_run': args.dry_run, 'fix_links': args.fix_links }
    # Process files in parallel.
    print('Starting pool')
    pool = Pool( args.num_processes )
    return_values = pool.map( partial( process_file, flags=flags ), file_list )
    print('Waiting to close pool')
    pool.close()
    pool.join()
    print('End of pool')
    # if all( return_values ):
    #     sys.exit( 0 )
    # sys.exit( 1 )

if __name__ == "__main__":
    main()

