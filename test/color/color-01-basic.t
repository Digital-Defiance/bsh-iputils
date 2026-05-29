#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Color output modes: CLI flags and basic ANSI vs truecolor detection.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $here = File::Basename::dirname(Cwd::abs_path($0));
require "$here/../lib.pl";
require "$here/lib-color.pl";

my $bping = get_cmd($ARGV[0] // 'bping');
my $probe = color_probe_tool($bping);

{
    my $cmd = run_color_cmd("$probe --no-color");
    $cmd->exit_is_num(0);
    subtest 'no-color strips escapes' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
        $cmd->stdout_like(qr/rtt min\/avg\/max\/mdev/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=always");
    $cmd->exit_is_num(0);
    subtest '--color=always on piped stdout' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=ansi");
    $cmd->exit_is_num(0);
    subtest 'ansi color mode' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
        $cmd->stdout_unlike(qr/38;2;/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=truecolor");
    $cmd->exit_is_num(0);
    subtest 'truecolor mode' => sub {
        $cmd->stdout_like(qr/38;2;/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color");
    $cmd->exit_is_num(0);
    subtest 'bare --color means always' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=auto");
    $cmd->exit_is_num(0);
    subtest 'auto mode disables color on piped stdout' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd($probe, IPUTILS_COLOR => 'never');
    $cmd->exit_is_num(0);
    subtest 'IPUTILS_COLOR=never' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd($probe, IPUTILS_COLOR => 'always');
    $cmd->exit_is_num(0);
    subtest 'IPUTILS_COLOR=always' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=nope");
    $cmd->exit_is_num(2);
    subtest 'invalid color mode' => sub {
        $cmd->stderr_like(qr/invalid color setting/);
    };
}

# btraceroute uses the same module; keep a secondary check when traceroute exists.
my $btraceroute = get_cmd('btraceroute');
my $have_traceroute = system("command -v traceroute >/dev/null 2>&1") == 0;

SKIP: {
    skip "traceroute required for btraceroute color smoke", 1 unless $have_traceroute;

    my $cmd = run_color_cmd("$btraceroute -m 1 -q 1 --color=truecolor 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'btraceroute truecolor' => sub {
        $cmd->stdout_like(qr/38;2;/);
        $cmd->stdout_like(qr/btraceroute to/);
    };
}

done_testing;
