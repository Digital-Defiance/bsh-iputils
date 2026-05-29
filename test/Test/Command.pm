#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Minimal Test::Command compatible shim for systems without perl-Test-Command
# (e.g. macOS). Uses only core Perl modules.

package Test::Command;

use strict;
use warnings;

use IPC::Open3 qw(open3);
use Symbol qw(gensym);

sub import {
	my ($class, @args) = @_;
	while (@args) {
		my $arg = shift @args;
		if ($arg eq 'tests') {
			require Test::More;
			my $n = shift @args;
			Test::More::plan(tests => $n) if defined $n;
		}
	}
}

sub _apply_env {
	my ($override) = @_;
	my @saved;
	for my $k (keys %$override) {
		push @saved, $k, $ENV{$k};
		$ENV{$k} = $override->{$k};
	}
	return \@saved;
}

sub _restore_env {
	my ($saved) = @_;
	while (@$saved) {
		my $v = pop @$saved;
		my $k = pop @$saved;
		if (defined $v) {
			$ENV{$k} = $v;
		} else {
			delete $ENV{$k};
		}
	}
}

sub new {
	my ($class, %opts) = @_;
	my $cmd = $opts{cmd} or die "Test::Command: cmd required";
	my $saved = _apply_env($opts{env} // {});

	my $stderr = gensym;
	my $pid = open3(undef, my $stdout_fh, $stderr, '/bin/sh', '-c', $cmd);

	local $/;
	my $stdout = defined(fileno($stdout_fh)) ? <$stdout_fh> : '';
	my $errout = defined(fileno($stderr)) ? <$stderr> : '';
	close $stdout_fh if defined $stdout_fh;
	close $stderr if defined fileno($stderr);

	waitpid($pid, 0);
	my $exit = $? >> 8;

	_restore_env($saved);

	return bless {
		stdout => defined $stdout ? $stdout : '',
		stderr => defined $errout ? $errout : '',
		exit   => $exit,
	}, $class;
}

sub stdout_value { shift->{stdout} }
sub stderr_value { shift->{stderr} }

sub exit_is_num {
	my ($self, $want, $name) = @_;
	require Test::More;
	Test::More::is($self->{exit}, $want, $name // "exit status $want");
}

sub stdout_like {
	my ($self, $re, $name) = @_;
	require Test::More;
	Test::More::like($self->{stdout}, $re, $name // 'stdout matches');
}

sub stdout_unlike {
	my ($self, $re, $name) = @_;
	require Test::More;
	Test::More::unlike($self->{stdout}, $re, $name // 'stdout does not match');
}

sub stderr_like {
	my ($self, $re, $name) = @_;
	require Test::More;
	Test::More::like($self->{stderr}, $re, $name // 'stderr matches');
}

sub stderr_is_eq {
	my ($self, $want, $name) = @_;
	require Test::More;
	Test::More::is($self->{stderr}, $want, $name // 'stderr exact match');
}

1;
