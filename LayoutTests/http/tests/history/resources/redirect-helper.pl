#!/usr/bin/perl
# Script to generate a 30x HTTP redirect (determined by the query parameter)
binmode STDOUT;

$REDIRECT_CODE = $ENV{'QUERY_STRING'} || '301';

$STATUS_TEXTS = {
  '301' => 'Moved Permanently',
  '302' => 'Moved Temporarily',
  '303' => 'See Other',
  '307' => 'Moved Temporarily'
};

$fragment = (($ENV{'HTTP_COOKIE'} || '') =~ /secondNavigation=true/) ? '#3' : '#2';

print "Status: $REDIRECT_CODE $STATUS_TEXTS{$REDIRECT_CODE}\r\n";
print "Location: redirect-target.html$fragment\r\n";
print "Content-type: text/html\r\n";
print "\r\n";

print <<HERE_DOC_END
<html>
<head>
<title>$REDIRECT_CODE Redirect</title>

<body>This page is a $REDIRECT_CODE redirect.</body>
</html>
HERE_DOC_END
