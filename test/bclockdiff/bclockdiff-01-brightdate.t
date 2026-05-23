#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# BrightDate test for bclockdiff.
#
# bclockdiff opens an IPPROTO_ICMP raw socket, which on stock Linux
# requires CAP_NET_RAW and on macOS requires root. When the test runs
# unprivileged, the socket() call returns EPERM and bclockdiff exits 1
# before any BrightDate output. We detect that path and skip the
# substantive assertion rather than spuriously fail the suite.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $lib = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
require "$lib";

my $bclockdiff = get_cmd($ARGV[0] // 'bclockdiff');

# Probe for raw-socket privilege by running once and inspecting stderr.
my $probe = Test::Command->new(cmd => "$bclockdiff --brightdate --unit=ud 127.0.0.1");
my $probe_err = $probe->stderr_value // '';
my $needs_priv = ($probe_err =~ /Operation not permitted|Permission denied/);

# --brightdate basic output. Unit `ud` (ASCII) is the canonical microday
# unit name. The output line printed by bclockdiff is:
#   BrightDate: rtt=N.NNNNNud delta=N.NNNNNud
{
    SKIP: {
        skip "bclockdiff needs CAP_NET_RAW / root", 2 if $needs_priv;

        my $cmd = Test::Command->new(cmd => "$bclockdiff --brightdate --unit=ud 127.0.0.1");
        $cmd->exit_is_num(0);
        subtest 'BrightDate output' => sub {
            $cmd->stdout_like(
                qr/BrightDate: rtt=[0-9.eE+-]+ud delta=[0-9.eE+-]+ud/,
                'prints BrightDate units');
        };
    }
}

done_testing;
