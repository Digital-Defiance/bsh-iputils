#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
#
# mock_geo_agent.pl — reusable BSH SDI v2 geo socket mock for Perl tests
#
# Implements the two-level path-indirection described in RFC SDI v2 §8:
#   $BSH_GEO_SOCK  →  path-file  →  Unix-domain socket  →  JSON response
#
# USAGE
#   require '/path/to/mock_geo_agent.pl';
#
#   # Start server with the default geo-context fixture
#   my ($path_file, $pid) = start_mock_geo_agent();
#
#   # Start server with a custom JSON response (or multiple responses)
#   my ($path_file, $pid) = start_mock_geo_agent(
#       responses => [ $json_string1, $json_string2 ],
#   );
#
#   # Run the tool under test
#   {
#       local $ENV{BSH_GEO_SOCK} = $path_file;
#       # ... Test::Command->new(...) ...
#   }
#
#   stop_mock_geo_agent($pid);   # reaps the child; safe to call even if child
#                                # already exited
#
# The server forks a child that accepts exactly one connection per listed
# response (default: 1), sends the response, and exits.  BSH_GEO_SOCK is NOT
# set by this library; the caller sets it with  local $ENV{BSH_GEO_SOCK}.

use strict;
use warnings;
use File::Temp qw(tempdir);
use IO::Socket::UNIX;
use POSIX qw(_exit);

our @EXPORT_OK = qw(
    start_mock_geo_agent
    stop_mock_geo_agent
    GEO_JSON_FULL
    GEO_JSON_GEODETIC_ONLY
);

# ── Default fixture: full geo-context with spacetime block ─────────────────
# Exercises the [sdi:ecef] code path.  BrightMeter ECEF values are from the
# reference location San Francisco, CA.
use constant GEO_JSON_FULL =>
    '{"type":"geo-context",' .
    '"geodetic":{"latitude":37.7749,"longitude":-122.4194,"altitude":100.0},' .
    '"ecef":{"x":-2711370.0,"y":-4260590.0,"z":3887440.0},' .
    '"spacetime":{"t":831492283.7,"x":-9.044e-3,"y":-1.421e-2,"z":1.297e-2},' .
    '"accuracy_metres":15.0,"provenance":"hardware","user_presence":true}';

# Fixture without spacetime block — exercises the [sdi] (lat/lon only) path.
use constant GEO_JSON_GEODETIC_ONLY =>
    '{"type":"geo-context",' .
    '"geodetic":{"latitude":37.7749,"longitude":-122.4194,"altitude":100.0},' .
    '"ecef":{"x":-2711370.0,"y":-4260590.0,"z":3887440.0},' .
    '"accuracy_metres":50.0,"provenance":"network","user_presence":true}';

# ── start_mock_geo_agent(%opts) ────────────────────────────────────────────
# Options:
#   responses  => \@list   — JSON strings to serve, one per connection.
#                            Defaults to [ GEO_JSON_FULL ].
#   tmpdir     => $path    — use an existing temp dir instead of creating one.
#
# Returns ($path_file, $child_pid).
sub start_mock_geo_agent {
    my (%opts) = @_;
    my @responses = @{ $opts{responses} // [ GEO_JSON_FULL ] };

    my $tmpdir    = $opts{tmpdir} // tempdir(CLEANUP => 1);
    my $sock_path = "$tmpdir/geo.sock";
    my $path_file = "$tmpdir/geo.sock.path";

    # Write socket path into the path-file (RFC SDI v2 §8.2 two-level indirection)
    open(my $fh, '>', $path_file) or die "mock_geo_agent: cannot write path-file: $!";
    print $fh "$sock_path\n";
    close $fh;

    my $server = IO::Socket::UNIX->new(
        Type   => SOCK_STREAM,
        Local  => $sock_path,
        Listen => 5,
    ) or die "mock_geo_agent: cannot create socket $sock_path: $!";

    my $pid = fork();
    die "mock_geo_agent: fork failed: $!" unless defined $pid;

    if ($pid == 0) {
        # Child: serve each configured response in sequence, then exit.
        for my $json (@responses) {
            my $conn = $server->accept();
            if ($conn) {
                # Consume the request line (newline-terminated JSON)
                my $req = '';
                while (defined(my $c = $conn->getc())) {
                    last if $c eq "\n";
                    $req .= $c;
                }
                print $conn $json . "\n";
                $conn->close();
            }
        }
        $server->close();
        _exit(0);
    }

    # Parent: close server fd so the child owns it exclusively.
    $server->close();
    return ($path_file, $pid);
}

# ── stop_mock_geo_agent($pid) ──────────────────────────────────────────────
# Reaps the child process.  Safe to call after the child has already exited.
sub stop_mock_geo_agent {
    my ($pid) = @_;
    waitpid($pid, 0) if defined $pid && $pid > 0;
}

1;
