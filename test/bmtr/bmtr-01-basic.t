#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests for bmtr: basic smoke + BSH SDI v2 geo socket
# Uses --report -c 1 to run a single cycle and print a clean report.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $lib  = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
my $mock = File::Basename::dirname(Cwd::abs_path($0)) . '/../mock_geo_agent.pl';
require "$lib";
require "$mock";

my $tool = get_cmd($ARGV[0] // 'bmtr');

# ── Smoke: no args ────────────────────────────────────────────
{
    my $cmd = Test::Command->new(cmd => "$tool");
    $cmd->exit_is_num(2);
    subtest 'help output' => sub {
        $cmd->stderr_like(qr/Usage/);
    };
}

# ── Smoke: help flag ──────────────────────────────────────────
{
    my $cmd = Test::Command->new(cmd => "$tool --help");
    $cmd->exit_is_num(2);
    subtest 'help flag' => sub {
        $cmd->stderr_like(qr/Usage/);
    };
}

# ── Basic run to localhost ────────────────────────────────────
{
    my $cmd = Test::Command->new(cmd => "$tool --report -c 1 -m 1 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'basic output' => sub {
        $cmd->stdout_like(qr/bmtr report/);
    };
}

# ── SDI v2 geo socket: [sdi] / [sdi:ecef] tag ────────────────
# Start a mock geo agent (see test/mock_geo_agent.pl), point BSH_GEO_SOCK at
# the path-file it creates, and verify the [sdi] or [sdi:ecef] tag appears.
{
    my ($path_file, $pid) = start_mock_geo_agent();

    {
        local $ENV{BSH_GEO_SOCK} = $path_file;
        my $cmd = Test::Command->new(cmd => "$tool --report -c 1 -m 1 127.0.0.1");
        $cmd->exit_is_num(0);
        $cmd->stdout_like(qr/\[sdi(?::ecef)?\]/, 'BSH_GEO_SOCK sets [sdi] or [sdi:ecef] tag');
    }

    stop_mock_geo_agent($pid);
}

done_testing;
