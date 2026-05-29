#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Color output: NO_COLOR, CLICOLOR*, COLORTERM, schemes, and flag precedence.

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
    my $cmd = run_color_cmd($probe, NO_COLOR => '1');
    $cmd->exit_is_num(0);
    subtest 'NO_COLOR disables auto mode' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd($probe, CLICOLOR => '0');
    $cmd->exit_is_num(0);
    subtest 'CLICOLOR=0 disables auto mode' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd($probe, CLICOLOR_FORCE => '1');
    $cmd->exit_is_num(0);
    subtest 'CLICOLOR_FORCE enables auto on piped stdout' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=always", COLORTERM => 'truecolor');
    $cmd->exit_is_num(0);
    subtest 'COLORTERM=truecolor selects RGB sequences' => sub {
        $cmd->stdout_like(qr/38;2;/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=ansi", COLORTERM => 'truecolor');
    $cmd->exit_is_num(0);
    subtest 'explicit ansi ignores COLORTERM' => sub {
        $cmd->stdout_like(qr/\x1b\[/);
        $cmd->stdout_unlike(qr/38;2;/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color-scheme=bright --color=truecolor");
    $cmd->exit_is_num(0);
    subtest 'bright truecolor palette' => sub {
        $cmd->stdout_like(qr/38;2;0;220;255/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color-scheme=default --color=truecolor");
    $cmd->exit_is_num(0);
    subtest 'default truecolor palette' => sub {
        $cmd->stdout_like(qr/38;2;80;200;220/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=truecolor", IPUTILS_COLOR_SCHEME => 'bright');
    $cmd->exit_is_num(0);
    subtest 'IPUTILS_COLOR_SCHEME=bright' => sub {
        $cmd->stdout_like(qr/38;2;0;220;255/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color-scheme=nope --color=always");
    $cmd->exit_is_num(2);
    subtest 'invalid color scheme' => sub {
        $cmd->stderr_like(qr/invalid color scheme/);
    };
}

{
    my $cmd = run_color_cmd("$probe --no-color", IPUTILS_COLOR => 'always');
    $cmd->exit_is_num(0);
    subtest '--no-color overrides IPUTILS_COLOR=always' => sub {
        $cmd->stdout_unlike(qr/\x1b\[/);
    };
}

{
    my $cmd = run_color_cmd("$probe --color=truecolor", IPUTILS_COLOR => 'ansi');
    $cmd->exit_is_num(0);
    subtest 'CLI --color overrides IPUTILS_COLOR' => sub {
        $cmd->stdout_like(qr/38;2;/);
    };
}

done_testing;
