#pragma once

DTree zip_list(String zip_file_name);
String zip_read(String zip_file_name, String entry_name);
bool zip_create(String zip_file_name, DTree entries);
bool zip_extract(String zip_file_name, String destination_directory);
String gz_compress(String src);
String gz_uncompress(String compressed);
