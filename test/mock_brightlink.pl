#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
#
# mock_brightlink.pl — start/stop the C-based mock-brightnexus binary
# from Perl tests.
#
# USAGE
#   require '/path/to/mock_brightlink.pl';
#
#   my ($sock_path, $pid) = start_mock_brightnexus();
#   {
#       local $ENV{BRIGHTNEXUS_SOCKET} = $sock_path;
#       local $ENV{HOME} = $tmp_home;        # so TOFU pin lands in tmp
#       # ... run iputils tool ...
#   }
#   stop_mock_brightnexus($pid);
#
# The helper forks/execs the mock binary built at
# $builddir/test/mock-brightnexus/mock-brightnexus, blocks until the mock
# prints its "ready" sync line, and returns the socket path it bound.

use strict;
use warnings;
use File::Temp qw(tempdir);
use IO::Handle;
use POSIX ":sys_wait_h";

our @EXPORT_OK = qw(start_mock_brightnexus stop_mock_brightnexus mock_home);

# Locate the mock binary inside the meson builddir. The convention used by
# get_cmd() in lib.pl is that the test is invoked with an absolute path to
# the tool under test as $ARGV[0]; we can derive the builddir from any of
# those tool paths. Caller passes the builddir explicitly to keep the
# helper self-contained.
sub _mock_binary {
    my ($builddir) = @_;
    # libBrightLink subproject build path under iputils builddir.
    my $candidate = "$builddir/subprojects/libbrightlink/tests/mock-brightnexus/mock-brightnexus";
    return $candidate if -x $candidate;

    # When invoked from inside meson test, the cwd is set to the project
    # source root and the builddir is supplied via $MESON_BUILD_ROOT.
    my $env = $ENV{MESON_BUILD_ROOT};
    if (defined $env && $env ne '') {
        my $alt = "$env/subprojects/libbrightlink/tests/mock-brightnexus/mock-brightnexus";
        return $alt if -x $alt;
    }
    return undef;
}

# Find the builddir by walking up from the tool path until we find the
# libBrightLink subproject (which only lives in the build tree).
sub _builddir_from_tool {
    my ($tool) = @_;
    my $dir = $tool;
    $dir =~ s|/[^/]+$||;     # strip last component
    while ($dir ne '' && $dir ne '/') {
        return $dir if -d "$dir/subprojects/libbrightlink";
        $dir =~ s|/[^/]+$||;
    }
    return undef;
}

sub mock_home {
    return tempdir(CLEANUP => 1);
}

# start_mock_brightnexus(\%opts)
#
#   Options:
#     tool_path => $path   — any iputils tool path, used to find builddir.
#                            REQUIRED if MESON_BUILD_ROOT is not set.
#
#   Returns ($sock_path, $pid) on success, (undef, undef) on failure.
sub start_mock_brightnexus {
    my (%opts) = @_;

    my $builddir;
    if ($opts{tool_path}) {
        $builddir = _builddir_from_tool($opts{tool_path});
    }
    $builddir //= $ENV{MESON_BUILD_ROOT};
    return (undef, undef) unless defined $builddir && $builddir ne '';

    my $mock_bin = _mock_binary($builddir);
    return (undef, undef) unless defined $mock_bin;

    my $tmpdir = tempdir(CLEANUP => 1);
    my $sock = "$tmpdir/brightnexus.sock";

    # Pipe so we can read the mock's "ready\n" sync line before letting
    # the test connect.
    pipe(my $rfh, my $wfh) or return (undef, undef);

    my $pid = fork();
    return (undef, undef) unless defined $pid;

    if ($pid == 0) {
        # Child: redirect stdout to the parent's pipe, then exec the mock.
        close $rfh;
        open(STDOUT, '>&', $wfh) or _exit_child("dup stdout: $!");
        close $wfh;
        # stderr stays attached to the test runner so unexpected failures
        # show up in the prove(1) log.
        exec($mock_bin, $sock) or _exit_child("exec $mock_bin: $!");
    }

    # Parent: wait for "ready\n" before returning.
    close $wfh;
    my $line = <$rfh>;
    close $rfh;
    if (!defined $line || $line !~ /^ready/) {
        kill 'TERM', $pid;
        waitpid($pid, 0);
        return (undef, undef);
    }
    return ($sock, $pid);
}

sub _exit_child {
    my ($msg) = @_;
    print STDERR "mock_brightlink child: $msg\n";
    POSIX::_exit(2);
}

# stop_mock_brightnexus($pid)
#
#   Politely terminates the mock and reaps it. Safe to call with undef
#   (no-op in that case).
sub stop_mock_brightnexus {
    my ($pid) = @_;
    return unless defined $pid && $pid > 0;
    kill 'TERM', $pid;
    # Bound the wait so a stuck mock can't hang the suite.
    my $deadline = time() + 5;
    while (time() < $deadline) {
        my $r = waitpid($pid, WNOHANG);
        return if $r == $pid || $r < 0;
        select(undef, undef, undef, 0.05);
    }
    kill 'KILL', $pid;
    waitpid($pid, 0);
}

1;
