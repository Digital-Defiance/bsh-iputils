#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests for baudit: basic smoke + BrightLink success/failure paths.

use Test::Command;
use Test::More;
use File::Basename;
use File::Temp qw(tempdir);
use Cwd;

my $here = File::Basename::dirname(Cwd::abs_path($0));
require "$here/../lib.pl";
require "$here/../mock_brightlink.pl";

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

# ── Bridge unreachable: graceful downgrade ────────────────────
# The tool MUST exit 0 and produce its normal output even when no bridge
# is listening. No [brightlink…] tag should appear in stdout.
{
    local $ENV{BRIGHTNEXUS_SOCKET} = '/nonexistent/brightnexus.sock';
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'bridge unreachable falls through' => sub {
        $cmd->stdout_like(qr/baudit/);
        $cmd->stdout_unlike(qr/\[brightlink/);
    };
}

# ── Mock bridge: [brightlink:ecef] tag flows through ──────────
SKIP: {
    my ($sock, $pid) = start_mock_brightnexus(tool_path => $tool);
    skip "mock-brightnexus not available", 1 unless defined $sock;

    my $home = mock_home();
    local $ENV{BRIGHTNEXUS_SOCKET} = $sock;
    local $ENV{HOME} = $home;   # TOFU pin lands in $home/.brightchain/...

    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[brightlink(?::ecef)?\]/, 'mock bridge yields [brightlink] tag');

    stop_mock_brightnexus($pid);
}

done_testing;
