#!/usr/bin/env python3
"""
UWB Protocol Log Analyzer
Analyzes INIT node logs to calculate correct OWN transmission statistics
Focus: SEQ 1 (OWN data transmission) only
"""

import re
import sys
from pathlib import Path
from collections import defaultdict

class UWBLogAnalyzer:
    def __init__(self, log_file_path):
        self.log_file_path = log_file_path
        self.cycles = defaultdict(lambda: {
            'period_1_line': 0,
            'period_2_line': 0,
            'period_3_line': 0,
            'next_cycle_line': 0,
            'period_1_own_success': False,
            'period_2_own_success': False,
            'period_3_own_success': False,
            'period_1_own_success_line': 0,
            'period_2_own_success_line': 0,
            'period_3_own_success_line': 0,
            'final_result': 'unknown',  # 'first_tx', 'first_retrans', 'second_retrans', 'failed'
            'fl_relay_attempts': 0,
            'fl_relay_success': False,
            'fr_relay_attempts': 0,
            'fr_relay_success': False,
            'missing_acks': []  # 각 period에서 어떤 노드의 ACK가 빠졌는지 추적
        })
        self.current_cycle = 0
        self.current_period = 0
        self.line_number = 0
        self.last_own_ack_complete_line = 0
        self.node_missing_stats = defaultdict(int)  # 노드별 missing ACK 통계

    def analyze(self):
        """Main analysis function - Focus on OWN transmission only"""
        with open(self.log_file_path, 'r') as f:
            for line in f:
                self.line_number += 1
                # Remove RTT prefix
                if '00> ' in line:
                    line = line.split('00> ', 1)[1]
                else:
                    continue

                # Track new cycle
                if 'NEW CYCLE' in line:
                    match = re.search(r'NEW CYCLE (\d+) started', line)
                    if match:
                        new_cycle_num = int(match.group(1))
                        # 1001번 cycle은 incomplete하므로 무시
                        if new_cycle_num > 1000:
                            self.current_cycle = 0  # Reset to ignore subsequent processing
                            continue
                        # Mark the end of previous cycle
                        if self.current_cycle > 0:
                            self.cycles[self.current_cycle]['next_cycle_line'] = self.line_number
                        self.current_cycle = new_cycle_num
                        self.current_period = 1  # NEW CYCLE은 항상 Period 1부터 시작
                        self.cycles[self.current_cycle]['period_1_line'] = self.line_number

                # Track period transitions
                if 'Period' in line and 'global period' in line:
                    match = re.search(r'Period (\d+) \(global period (\d+)\)', line)
                    if match:
                        period_num = int(match.group(1))
                        if self.current_cycle > 0:
                            if period_num == 2:
                                self.cycles[self.current_cycle]['period_2_line'] = self.line_number
                            elif period_num == 3:
                                self.cycles[self.current_cycle]['period_3_line'] = self.line_number
                        self.current_period = period_num

                # Track OWN Missing ACKs from specific nodes
                if 'OWN Missing ACKs from:' in line and self.current_cycle > 0:
                    missing_match = re.search(r'OWN Missing ACKs from: ([0-9A-F ]+)', line)
                    if missing_match:
                        missing_nodes_str = missing_match.group(1)
                        missing_nodes = missing_nodes_str.split()

                        # Check which of our target nodes (4, 5, 9, A) are missing
                        target_nodes = {'4', '5', '9', 'A'}
                        missing_target_nodes = []

                        for node in missing_nodes:
                            if node in target_nodes:
                                missing_target_nodes.append(node)
                                self.node_missing_stats[node] += 1

                        if missing_target_nodes:
                            self.cycles[self.current_cycle]['missing_acks'].append({
                                'period': self.current_period,
                                'line': self.line_number,
                                'missing_nodes': missing_target_nodes,
                                'type': 'explicit_missing'
                            })

                # Track OWN ACK collection COMPLETE (OWN 전송 성공의 첫 번째 신호)
                if 'OWN ACK collection COMPLETE' in line and self.current_cycle > 0:
                    self.last_own_ack_complete_line = self.line_number

                # Track TRANSMISSION SUCCESS - All ACKs received (OWN 전송 성공의 확정 신호)
                if 'TRANSMISSION SUCCESS - All ACKs received' in line and self.current_cycle > 0:
                    # OWN ACK COMPLETE 바로 다음에 나오는 TRANSMISSION SUCCESS만 OWN 성공으로 판단
                    if self.last_own_ack_complete_line > 0 and (self.line_number - self.last_own_ack_complete_line) <= 5:
                        if self.current_period == 1:
                            self.cycles[self.current_cycle]['period_1_own_success'] = True
                            self.cycles[self.current_cycle]['period_1_own_success_line'] = self.line_number
                        elif self.current_period == 2:
                            self.cycles[self.current_cycle]['period_2_own_success'] = True
                            self.cycles[self.current_cycle]['period_2_own_success_line'] = self.line_number
                        elif self.current_period == 3:
                            self.cycles[self.current_cycle]['period_3_own_success'] = True
                            self.cycles[self.current_cycle]['period_3_own_success_line'] = self.line_number
                        self.last_own_ack_complete_line = 0  # Reset


                # Track relay success (for relay statistics)
                if 'FL_RELAY ACK collection COMPLETE' in line:
                    self.cycles[self.current_cycle]['fl_relay_attempts'] += 1
                    self.cycles[self.current_cycle]['fl_relay_success'] = True
                if 'FR_RELAY ACK collection COMPLETE' in line:
                    self.cycles[self.current_cycle]['fr_relay_attempts'] += 1
                    self.cycles[self.current_cycle]['fr_relay_success'] = True

        # Post-processing: 최종 결과 분류
        self._classify_cycle_results()

    def _classify_cycle_results(self):
        """Post-processing: 각 사이클의 최종 결과 분류"""
        for cycle_num in sorted([c for c in self.cycles.keys() if c > 0]):
            cycle = self.cycles[cycle_num]

            # Period 2나 3로 진행했는지 확인
            period_2_started = cycle['period_2_line'] > 0
            period_3_started = cycle['period_3_line'] > 0

            if cycle['period_1_own_success']:
                if not period_2_started:
                    # Period 1에서 성공하고 Period 2로 가지 않음 = First TX Success
                    cycle['final_result'] = 'first_tx'
                else:
                    # Period 1에서 성공했는데 Period 2로 갔다? 이상함, 일단 first_tx로 분류
                    cycle['final_result'] = 'first_tx'
            elif cycle['period_2_own_success']:
                if not period_3_started:
                    # Period 1 실패, Period 2 성공, Period 3로 가지 않음 = First Retrans Success
                    cycle['final_result'] = 'first_retrans'
                else:
                    # Period 2에서 성공했는데 Period 3으로 갔다? 이상함, 일단 first_retrans로 분류
                    cycle['final_result'] = 'first_retrans'
            elif cycle['period_3_own_success']:
                # Period 1, 2 실패, Period 3 성공 = Second Retrans Success
                cycle['final_result'] = 'second_retrans'
            else:
                # Period 1, 2, 3 모두 실패 = Failed
                cycle['final_result'] = 'failed'

    def calculate_statistics(self):
        """Calculate final statistics from analyzed data"""
        # 마지막 incomplete cycle (1001) 제외
        valid_cycles = [c for c in self.cycles.keys() if c > 0 and c <= 1000]
        total_cycles = len(valid_cycles)
        first_tx_success = 0
        first_retrans_success = 0
        second_retrans_success = 0
        failed_cycles = 0
        fl_relay_attempts = 0
        fl_relay_success = 0
        fr_relay_attempts = 0
        fr_relay_success = 0

        # Detailed cycle analysis
        cycle_details = []

        for cycle_num in sorted(valid_cycles):
            cycle = self.cycles[cycle_num]

            # Count by final result
            if cycle['final_result'] == 'first_tx':
                first_tx_success += 1
            elif cycle['final_result'] == 'first_retrans':
                first_retrans_success += 1
            elif cycle['final_result'] == 'second_retrans':
                second_retrans_success += 1
            elif cycle['final_result'] == 'failed':
                failed_cycles += 1

            # Relay statistics
            if cycle['fl_relay_attempts'] > 0:
                fl_relay_attempts += cycle['fl_relay_attempts']
                if cycle['fl_relay_success']:
                    fl_relay_success += 1

            if cycle['fr_relay_attempts'] > 0:
                fr_relay_attempts += cycle['fr_relay_attempts']
                if cycle['fr_relay_success']:
                    fr_relay_success += 1

            # Store cycle details for debugging
            cycle_details.append({
                'cycle': cycle_num,
                'period_2_started': cycle['period_2_line'] > 0,
                'period_3_started': cycle['period_3_line'] > 0,
                'period_1_success': cycle['period_1_own_success'],
                'period_2_success': cycle['period_2_own_success'],
                'period_3_success': cycle['period_3_own_success'],
                'final_result': cycle['final_result'],
                'missing_acks': cycle['missing_acks']
            })

        successful_cycles = first_tx_success + first_retrans_success + second_retrans_success

        return {
            'total_cycles': total_cycles,
            'successful_cycles': successful_cycles,
            'first_tx_success': first_tx_success,
            'first_retrans_success': first_retrans_success,
            'second_retrans_success': second_retrans_success,
            'failed_cycles': failed_cycles,
            'fl_relay_attempts': fl_relay_attempts,
            'fl_relay_success': fl_relay_success,
            'fr_relay_attempts': fr_relay_attempts,
            'fr_relay_success': fr_relay_success,
            'cycle_details': cycle_details[:20],  # First 20 cycles for debugging
            'node_missing_stats': dict(self.node_missing_stats)  # 노드별 missing ACK 통계
        }

    def print_statistics(self, stats):
        """Print statistics in formatted output"""
        print("\n" + "="*70)
        print("=== CORRECTED OWN TRANSMISSION STATISTICS (SEQ 1 ONLY) ===")
        print("="*70)

        print("\n=== OWN DATA TRANSMISSION (SEQ 1) ===")
        print(f"Total Cycles: {stats['total_cycles']}")
        print(f"Successful Cycles: {stats['successful_cycles']}")
        success_rate = (stats['successful_cycles'] / stats['total_cycles'] * 100) if stats['total_cycles'] > 0 else 0
        print(f"Success Rate: {success_rate:.2f}%")
        print()
        print(f"First TX Success (Period 1): {stats['first_tx_success']}")
        print(f"First Retrans Success (Period 2): {stats['first_retrans_success']}")
        print(f"Second Retrans Success (Period 3): {stats['second_retrans_success']}")
        print(f"Failed Cycles: {stats['failed_cycles']}")
        print()

        # Percentage breakdown
        if stats['total_cycles'] > 0:
            first_tx_rate = (stats['first_tx_success'] / stats['total_cycles'] * 100)
            first_retrans_rate = (stats['first_retrans_success'] / stats['total_cycles'] * 100)
            second_retrans_rate = (stats['second_retrans_success'] / stats['total_cycles'] * 100)
            failed_rate = (stats['failed_cycles'] / stats['total_cycles'] * 100)

            print("=== BREAKDOWN BY PERCENTAGE ===")
            print(f"First TX Success: {first_tx_rate:.1f}%")
            print(f"First Retrans Success: {first_retrans_rate:.1f}%")
            print(f"Second Retrans Success: {second_retrans_rate:.1f}%")
            print(f"Failed: {failed_rate:.1f}%")

        print("\n=== RELAY STATISTICS ===")
        print(f"FL Relay Attempts: {stats['fl_relay_attempts']}")
        print(f"FL Relay Success: {stats['fl_relay_success']}")
        fl_rate = (stats['fl_relay_success'] / stats['fl_relay_attempts'] * 100) if stats['fl_relay_attempts'] > 0 else 0
        print(f"FL Relay Rate: {fl_rate:.2f}%")

        print(f"FR Relay Attempts: {stats['fr_relay_attempts']}")
        print(f"FR Relay Success: {stats['fr_relay_success']}")
        fr_rate = (stats['fr_relay_success'] / stats['fr_relay_attempts'] * 100) if stats['fr_relay_attempts'] > 0 else 0
        print(f"FR Relay Rate: {fr_rate:.2f}%")

        total_relay_attempts = stats['fl_relay_attempts'] + stats['fr_relay_attempts']
        total_relay_success = stats['fl_relay_success'] + stats['fr_relay_success']
        overall_rate = (total_relay_success / total_relay_attempts * 100) if total_relay_attempts > 0 else 0
        print(f"Total Relay Attempts: {total_relay_attempts}")
        print(f"Total Relay Success: {total_relay_success}")
        print(f"Overall Relay Rate: {overall_rate:.2f}%")

        print("\n=== SAMPLE CYCLE DETAILS (First 20) ===")
        for detail in stats['cycle_details']:
            p1_success = "✓" if detail['period_1_success'] else "✗"
            p2_success = "✓" if detail['period_2_success'] else "✗"
            p3_success = "✓" if detail['period_3_success'] else "✗"
            p2_started = "Y" if detail['period_2_started'] else "N"
            p3_started = "Y" if detail['period_3_started'] else "N"

            missing_info = ""
            if detail['missing_acks']:
                missing_info = f" [Missing: {len(detail['missing_acks'])} issues]"

            print(f"Cycle {detail['cycle']}: P1={p1_success} P2={p2_success}({p2_started}) P3={p3_success}({p3_started}) → {detail['final_result'].upper()}{missing_info}")

    def count_patterns(self):
        """Count specific patterns in the log"""
        patterns = {
            'OWN FIRST TX': 0,
            'OWN RETRANSMISSION': 0,
            'OWN SKIP': 0,
            'FL RELAY FIRST TX': 0,
            'FL RELAY RETRANS': 0,
            'FR RELAY FIRST TX': 0,
            'FR RELAY RETRANS': 0,
            'TRANSMISSION SUCCESS': 0,
            'OWN FIRST TX FAILED': 0,
            'OWN RETRANS FAILED': 0
        }

        with open(self.log_file_path, 'r') as f:
            for line in f:
                for pattern in patterns:
                    if pattern in line:
                        patterns[pattern] += 1

        print("\n=== PATTERN COUNTS ===")
        for pattern, count in patterns.items():
            print(f"{pattern}: {count}")

        return patterns

def main():
    # Default log file path
    log_path = "/Users/minjae/Desktop/UWB-RX 2/DW3_QM33_SDK_1.0.2/Drivers/API/UWB_PROTOCOL_TEST_LOG_1001/INIT_NODE/initnode_driving2.log"

    # Allow custom path as argument
    if len(sys.argv) > 1:
        log_path = sys.argv[1]

    # Check if file exists
    if not Path(log_path).exists():
        print(f"Error: Log file not found: {log_path}")
        sys.exit(1)

    print(f"Analyzing log file: {log_path}")

    # Create analyzer and run analysis
    analyzer = UWBLogAnalyzer(log_path)

    # Count patterns first
    patterns = analyzer.count_patterns()

    # Analyze log
    analyzer.analyze()

    # Calculate and print statistics
    stats = analyzer.calculate_statistics()
    analyzer.print_statistics(stats)

    # Print analysis summary
    print("\n=== ANALYSIS SUMMARY ===")
    print(f"OWN RETRANSMISSION count: {patterns['OWN RETRANSMISSION']}")
    print(f"Calculated First Retrans Success: {stats['first_retrans_success']}")
    print(f"Calculated Second Retrans Success: {stats['second_retrans_success']}")
    print(f"Total Retrans Success: {stats['first_retrans_success'] + stats['second_retrans_success']}")

    # Missing ACK analysis
    print("\n=== MISSING ACK ANALYSIS ===")
    total_missing_events = 0
    for detail in stats['cycle_details']:
        if detail['missing_acks']:
            total_missing_events += len(detail['missing_acks'])

    print(f"Total Missing ACK Events: {total_missing_events}")

    # 노드별 Missing ACK 통계
    print(f"\n=== NODE-SPECIFIC MISSING ACK STATISTICS ===")
    node_stats = stats['node_missing_stats']
    target_nodes = ['4', '5', '9', 'A']

    print(f"Target nodes: {', '.join(target_nodes)}")
    for node in target_nodes:
        count = node_stats.get(node, 0)
        print(f"  Node {node}: {count} times failed to receive ACK")

    if total_missing_events > 0:
        print("\nSample Missing ACK Events (First 10):")
        count = 0
        for detail in stats['cycle_details']:
            if detail['missing_acks'] and count < 10:
                for missing in detail['missing_acks']:
                    if missing['type'] == 'explicit_missing':
                        nodes_list = ', '.join(missing['missing_nodes'])
                        print(f"  Cycle {detail['cycle']}, Period {missing['period']}: Missing from nodes [{nodes_list}]")
                    count += 1
                    if count >= 10:
                        break

    print(f"")
    print(f"This analysis focuses only on OWN data transmission (SEQ 1)")
    print(f"Relay transmissions (SEQ 10, 12) are tracked separately for relay statistics")

if __name__ == "__main__":
    main()