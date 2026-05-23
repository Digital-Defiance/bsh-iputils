#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests for bmtr: basic smoke + BrightLink success/failure paths.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $here = File::Basename::dirname(Cwd::abs_path($0));
require "$here/../lib.pl";
require "$here/../mock_brightlink.pl";

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

# ── Bridge unreachable: graceful downgrade ────────────────────
{
    local $ENV{BRIGHTNEXUS_SOCKET} = '/nonexistent/brightnexus.sock';
    my $cmd = Test::Command->new(cmd => "$tool --report -c 1 -m 1 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'bridge unreachable falls through' => sub {
        $cmd->stdout_like(qr/bmtr report/);
        $cmd->stdout_unlike(qr/\[brightlink/);
    };
}

# ── Mock bridge: [brightlink:ecef] tag flows through ──────────
SKIP: {
    my ($sock, $pid) = start_mock_brightnexus(tool_path => $tool);
    skip "mock-brightnexus not available", 1 unless defined $sock;

    my $home = mock_home();
    local $ENV{BRIGHTNEXUS_SOCKET} = $sock;
    local $ENV{HOME} = $home;

    my $cmd = Test::Command->new(cmd => "$tool --report -c 1 -m 1 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[brightlink(?::ecef)?\]/, 'mock bridge yields [brightlink] tag');

    stop_mock_brightnexus($pid);
}

done_testing;
