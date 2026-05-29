#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Color output tests for b* tools (shared iputils_color module).

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $here = File::Basename::dirname(Cwd::abs_path($0));
require "$here/../lib.pl";

my $tool = get_cmd($ARGV[0] // 'btraceroute');
my $have_traceroute = system("command -v traceroute >/dev/null 2>&1") == 0;

sub run_tool {
    my ($extra) = @_;
    return Test::Command->new(cmd => "$tool -m 1 -q 1 $extra 127.0.0.1");
}

SKIP: {
    skip "traceroute required for color smoke test", 5 unless $have_traceroute;

    {
        my $cmd = run_tool('--no-color');
        $cmd->exit_is_num(0);
        subtest 'no-color strips escapes' => sub {
            $cmd->stdout_unlike(qr/\x1b\[/);
            $cmd->stdout_like(qr/btraceroute to/);
        };
    }

    {
        my $cmd = run_tool('--color=ansi');
        $cmd->exit_is_num(0);
        subtest 'ansi color mode' => sub {
            $cmd->stdout_like(qr/\x1b\[/);
            $cmd->stdout_unlike(qr/38;2;/);
        };
    }

    {
        my $cmd = run_tool('--color=truecolor');
        $cmd->exit_is_num(0);
        subtest 'truecolor mode' => sub {
            $cmd->stdout_like(qr/38;2;/);
        };
    }

    {
        local $ENV{IPUTILS_COLOR} = 'never';
        my $cmd = run_tool('');
        $cmd->exit_is_num(0);
        subtest 'IPUTILS_COLOR=never' => sub {
            $cmd->stdout_unlike(qr/\x1b\[/);
        };
    }

    {
        my $cmd = run_tool('--color=nope');
        $cmd->exit_is_num(2);
        subtest 'invalid color mode' => sub {
            $cmd->stderr_like(qr/invalid color setting/);
        };
    }
}

done_testing;
