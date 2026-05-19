#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Tests for baudit: basic smoke + full env var priority chain.
#
# When BSPACE_COORD (or another env var) is set and ping to 127.0.0.1
# succeeds, baudit builds a self-anchor whose label includes my_geo.tag.
# The --anchor=0,0,5 flag ensures at least one anchor is always present
# so the audit table is printed regardless of network availability.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $lib = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
require "$lib";

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

# ── Env var tier 2: BSPACE_COORD ────────────────────────────
{
    local $ENV{BSPACE_COORD} = '37.7749,-122.4194';
    delete local $ENV{BSPACE_ECEF};
    delete local $ENV{BSPACE_LAT};
    delete local $ENV{BSPACE_LON};
    delete local $ENV{LAT};
    delete local $ENV{LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[env\]/, 'BSPACE_COORD sets [env] tag');
}

# ── Env var tier 3: BSPACE_LAT + BSPACE_LON ──────────────────
{
    local $ENV{BSPACE_LAT} = '37.7749';
    local $ENV{BSPACE_LON} = '-122.4194';
    delete local $ENV{BSPACE_ECEF};
    delete local $ENV{BSPACE_COORD};
    delete local $ENV{LAT};
    delete local $ENV{LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[env:ll\]/, 'BSPACE_LAT+BSPACE_LON sets [env:ll] tag');
}

# ── Env var tier 4: LAT + LON ─────────────────────────────────
{
    local $ENV{LAT} = '37.7749';
    local $ENV{LON} = '-122.4194';
    delete local $ENV{BSPACE_ECEF};
    delete local $ENV{BSPACE_COORD};
    delete local $ENV{BSPACE_LAT};
    delete local $ENV{BSPACE_LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[ll\]/, 'LAT+LON sets [ll] tag');
}

# ── Env var tier 1: BSPACE_ECEF ──────────────────────────────
{
    local $ENV{BSPACE_ECEF} = '0.02125,0,0';
    delete local $ENV{BSPACE_COORD};
    delete local $ENV{BSPACE_LAT};
    delete local $ENV{BSPACE_LON};
    delete local $ENV{LAT};
    delete local $ENV{LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[ecef\]/, 'BSPACE_ECEF sets [ecef] tag');
}

# ── Priority: BSPACE_ECEF beats BSPACE_COORD ─────────────────
{
    local $ENV{BSPACE_ECEF}  = '0.02125,0,0';
    local $ENV{BSPACE_COORD} = '37.7749,-122.4194';
    delete local $ENV{BSPACE_LAT};
    delete local $ENV{BSPACE_LON};
    delete local $ENV{LAT};
    delete local $ENV{LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[ecef\]/, 'BSPACE_ECEF beats BSPACE_COORD');
}

# ── Priority: BSPACE_COORD beats BSPACE_LAT+BSPACE_LON ───────
{
    local $ENV{BSPACE_COORD} = '37.7749,-122.4194';
    local $ENV{BSPACE_LAT}   = '51.5074';
    local $ENV{BSPACE_LON}   = '-0.1278';
    delete local $ENV{BSPACE_ECEF};
    delete local $ENV{LAT};
    delete local $ENV{LON};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[env\]/, 'BSPACE_COORD beats BSPACE_LAT+BSPACE_LON');
}

# ── Priority: BSPACE_LAT+BSPACE_LON beats LAT+LON ────────────
{
    local $ENV{BSPACE_LAT} = '37.7749';
    local $ENV{BSPACE_LON} = '-122.4194';
    local $ENV{LAT}        = '51.5074';
    local $ENV{LON}        = '-0.1278';
    delete local $ENV{BSPACE_ECEF};
    delete local $ENV{BSPACE_COORD};
    my $cmd = Test::Command->new(cmd => "$tool -c 1 --anchor=0,0,5 127.0.0.1");
    $cmd->exit_is_num(0);
    $cmd->stdout_like(qr/\[env:ll\]/, 'BSPACE_LAT+BSPACE_LON beats LAT+LON');
}

done_testing;
