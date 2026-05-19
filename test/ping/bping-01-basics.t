#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 BrightChain Contributors

use Test::Command tests => 6;
use Test::More;

my $lib = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
require "$lib";

my $bping = get_cmd($ARGV[0] // 'bping');

# Test: No args
{
    my $cmd = Test::Command->new(cmd => "$bping");
    $cmd->exit_is_num(2);
    subtest 'output' => sub {
        $cmd->stderr_like(qr/Usage/);
    }
}

# Test: Only destination, no coords
{
    my $cmd = Test::Command->new(cmd => "$bping 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'output' => sub {
        $cmd->stdout_like(qr/BrightSpace audit/);
        $cmd->stdout_like(qr/bping: Both --my-coord and --target-coord must be provided/);
    }
}

# Test: Invalid coords
{
    my $cmd = Test::Command->new(cmd => "$bping --my-coord=foo,bar,baz --target-coord=1,2,3 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'output' => sub {
        $cmd->stdout_like(qr/bping: Both --my-coord and --target-coord must be provided/);
    }
}

# Test: Valid coords, local ping
{
    my $cmd = Test::Command->new(cmd => "$bping --my-coord=0,0,0 --target-coord=0,0,0 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'output' => sub {
        $cmd->stdout_like(qr/Distance: +?0.000 mbm/);
        $cmd->stdout_like(qr/Latency: +?\d+\.\d+ ms/);
        $cmd->stdout_like(qr/Efficiency: \d+\.\d+%/);
    }
}

# Test: Valid coords, unreachable host
{
    my $cmd = Test::Command->new(cmd => "$bping --my-coord=0,0,0 --target-coord=1,1,1 192.0.2.1");
    $cmd->exit_is_num(0);
    subtest 'output' => sub {
        $cmd->stdout_like(qr/bping: Could not measure latency/);
    }
}

# Test: Version/help
{
    my $cmd = Test::Command->new(cmd => "$bping --help");
    $cmd->exit_is_num(2);
    subtest 'output' => sub {
        $cmd->stderr_like(qr/Usage/);
    }
}
