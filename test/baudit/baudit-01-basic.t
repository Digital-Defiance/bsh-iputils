#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests for baudit: basic smoke + BSH SDI v2 geo socket
#
# When BSH_GEO_SOCK is set and ping to 127.0.0.1 succeeds, baudit builds a
# self-anchor whose label includes my_geo.tag.  The --anchor=0,0,5 flag
# ensures at least one anchor is always present so the audit table is printed
# regardless of network availability.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $lib  = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
my $mock = File::Basename::dirname(Cwd::abs_path($0)) . '/../mock_geo_agent.pl';
require "$lib";
require "$mock";

my $tool = get_cmd($ARGV[0] // 'baudit');

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

# ── Basic run to localhost with one anchor ────────────────────
{
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'basic output' => sub {
        $cmd->stdout_like(qr/baudit/);
    };
}

# ── SDI v2 geo socket: [sdi] / [sdi:ecef] tag ────────────────
# Start a mock geo agent (see test/mock_geo_agent.pl), point BSH_GEO_SOCK at
# the path-file it creates, and verify the [sdi] or [sdi:ecef] tag appears.
{
    my ($path_file, $pid) = start_mock_geo_agent();

    {
        local $ENV{BSH_GEO_SOCK} = $path_file;
        my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
        $cmd->exit_is_num(0);
        $cmd->stdout_like(qr/\[sdi(?::ecef)?\]/, 'BSH_GEO_SOCK sets [sdi] or [sdi:ecef] tag');
    }

    stop_mock_geo_agent($pid);
}

done_testing;
