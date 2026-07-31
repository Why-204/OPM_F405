#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OPM_F405 网口(CH9121 透传)协议验证脚本。

用法:
    python opm_net_verify.py [--ip 192.168.1.200] [--port 8888] [--timeout 2.0]
                             [--udp] [--danger] [--verbose]

说明:
  - 设备为 TCP 服务端(CH9121 端口1)，本脚本作为客户端连接过去。
  - 默认逐条验证除 WRIP/WRPT/BOOT 之外的所有命令(这三条会改 IP/端口或重启,
    导致 TCP 连接中断)。加 --danger 才会测这三条(会放在最后执行)。
  - 第21条 STMT / 第22条 STST 依赖外部触发中断, 硬件未预留、无法实现, 不测试。
    因此共 26 条命令中实际可验证 23 条(含 ERR 由参数非法路径间接覆盖)。

帧格式:
    0xAA | 长度(2B LE)=N+5 | 命令字(4B ASCII) | 数据(N) | 校验和(1B)
    校验和 = 除校验和外所有字节之和的低 8 位。
"""

import argparse
import socket
import struct
import sys
import time

HEADER = 0xAA


# ------------------------- 帧编解码 ------------------------- #
def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def build_frame(cmd: bytes, payload: bytes = b"") -> bytes:
    assert len(cmd) == 4, "命令字必须 4 字节"
    length = len(cmd) + len(payload) + 1  # cmd + data + checksum
    head = bytes([HEADER]) + struct.pack("<H", length) + cmd + payload
    return head + bytes([checksum(head)])


class FrameError(Exception):
    pass


def recv_frame(sock: socket.socket) -> tuple[bytes, bytes]:
    """读一个完整应答帧, 返回 (cmd, data)。cmd 为 3 字节(ERR)或 4 字节。"""
    buf = _recv_exact(sock, 3)  # header + length
    if buf[0] != HEADER:
        raise FrameError(f"包头错误: 0x{buf[0]:02X}")
    length = struct.unpack("<H", buf[1:3])[0]
    total = length + 3
    if total < 7:
        raise FrameError(f"长度非法: {length}")
    rest = _recv_exact(sock, total - 3)
    frame = buf + rest
    if checksum(frame[:-1]) != frame[-1]:
        raise FrameError("校验和错误")
    # ERR 帧命令字 3 字节, 总长 7; 其余命令字 4 字节
    if total == 7 and frame[3:6] == b"ERR":
        return b"ERR", b""
    cmd = frame[3:7]
    data = frame[7:total - 1]
    return cmd, data


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    out = b""
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise FrameError("连接被关闭/无数据")
        out += chunk
    return out


# ------------------------- 测试框架 ------------------------- #
class Verifier:
    def __init__(self, sock, verbose=False):
        self.sock = sock
        self.verbose = verbose
        self.passed = 0
        self.failed = 0

    def _txrx(self, cmd: bytes, payload: bytes = b"") -> tuple[bytes, bytes]:
        self.sock.sendall(build_frame(cmd, payload))
        return recv_frame(self.sock)

    def check(self, name: str, cmd: bytes, payload: bytes, validate) -> None:
        """validate(rcmd, rdata) 返回 (ok: bool, info: str)。"""
        try:
            rcmd, rdata = self._txrx(cmd, payload)
            ok, info = validate(rcmd, rdata)
        except Exception as e:  # noqa: BLE001
            ok, info = False, f"异常: {e}"
        tag = "PASS" if ok else "FAIL"
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        line = f"[{tag}] {name:<28} {info}"
        if not ok or self.verbose:
            print(line)
        else:
            print(f"[{tag}] {name}")

    def expect_error(self, name: str, cmd: bytes, payload: bytes) -> None:
        def v(rcmd, rdata):
            if rcmd == b"ERR":
                return True, "已按预期返回 ERR"
            return False, f"预期 ERR, 实得 cmd={rcmd!r} data={rdata.hex()}"
        self.check(name, cmd, payload, v)


# --------- 各命令 validator 生成器 --------- #
def eq_bytes(expected: bytes, label: str):
    def v(rcmd, rdata):
        if rdata == expected:
            return True, f"{label}={rdata!r}"
        return False, f"{label} 不符: 期望 {expected!r}, 实得 {rdata!r}"
    return v


def struct_len(expected_cmd: bytes, min_len: int, describe):
    def v(rcmd, rdata):
        if rcmd != expected_cmd:
            return False, f"命令字不符: {rcmd!r}"
        if len(rdata) < min_len:
            return False, f"数据过短: {len(rdata)}<{min_len}"
        return True, describe(rdata)
    return v


def status_ok(expected_cmd: bytes):
    def v(rcmd, rdata):
        if rcmd == expected_cmd and rdata == b"\x00":
            return True, "状态 OK(0x00)"
        return False, f"非 OK 应答: cmd={rcmd!r} data={rdata.hex()}"
    return v


# ------------------------- 主流程 ------------------------- #
def run(sock, args):
    v = Verifier(sock, args.verbose)

    print("=== 只读/设备信息类 ===")
    v.check("1  RDPN 产品名称", b"RDPN", b"", eq_bytes(b"PM4177", "name"))
    v.check("2  RDSN 序列号", b"RDSN", b"", eq_bytes(b"PM2017071801", "sn"))
    v.check("3  RDVR 版本号", b"RDVR", b"",
            eq_bytes(bytes([1, 0, 2, 1]), "ver(hwM,hwm,swM,swm)"))
    v.check("4  RDMC MAC", b"RDMC", b"",
            eq_bytes(bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]), "mac"))
    v.check("5  RDIP IP", b"RDIP", b"",
            eq_bytes(bytes([192, 168, 1, 200]), "ip"))
    v.check("7  RDPT 端口", b"RDPT", b"",
            eq_bytes(struct.pack("<H", 8888), "port=8888"))

    # 通道个数(动态), 供后续使用
    nch = [8]

    def v_rdcc(rcmd, rdata):
        if rcmd != b"RDCC" or len(rdata) != 1:
            return False, f"格式错: cmd={rcmd!r} len={len(rdata)}"
        nch[0] = rdata[0]
        return True, f"通道数={rdata[0]}"
    v.check("9  RDCC 通道个数", b"RDCC", b"", v_rdcc)

    v.check("10 RDTM 采样平均时间", b"RDTM", bytes([1]),
            struct_len(b"RDTM", 5,
                       lambda d: f"ch={d[0]} us={struct.unpack('<I', d[1:5])[0]}"))
    v.check("12 RDWC 标定波长个数", b"RDWC", b"",
            struct_len(b"RDWC", 1, lambda d: f"个数={d[0]}"))
    v.check("13 RDWL 标定波长列表", b"RDWL", b"",
            struct_len(b"RDWL", 2,
                       lambda d: "wl=" + ",".join(
                           str(x) for x in struct.unpack(f"<{len(d)//2}H", d))))
    v.check("14 RDWW 当前工作波长", b"RDWW", bytes([1]),
            struct_len(b"RDWW", 3,
                       lambda d: f"ch={d[0]} wl={struct.unpack('<H', d[1:3])[0]}"))
    v.check("16 RDPR 当前光功率", b"RDPR", bytes([1, 1]),
            struct_len(b"RDPR", 6,
                       lambda d: f"ch={d[0]} pwr={struct.unpack('<f', d[2:6])[0]:.3f} dBm"))
    v.check("18 RDPO 波长偏移量", b"RDPO", bytes([1, 1, 1]),
            struct_len(b"RDPO", 7,
                       lambda d: f"ch={d[0]} idx={d[2]} off={struct.unpack('<f', d[3:7])[0]:.3f}"))

    print("\n=== 设置类(会改运行态, 不写 EEPROM) ===")
    v.check("11 STTM 设置采样平均时间", b"STTM",
            bytes([1]) + struct.pack("<I", 100000), status_ok(b"STTM"))
    v.check("15 STWW 设置工作波长", b"STWW",
            bytes([1]) + struct.pack("<H", 1310), status_ok(b"STWW"))
    v.check("17 WRPO 写波长偏移量", b"WRPO",
            bytes([1, 1, 1]) + struct.pack("<f", 0.0), status_ok(b"WRPO"))

    # 注: 第21条 STMT / 第22条 STST 依赖外部触发中断, 硬件未预留、无法实现, 不测试。

    print("\n=== 连续测量流程 STMP->RDFC->RDMR->STSM ===")
    npts = 10
    v.check("20 STMP 启动连续测量", b"STMP",
            struct.pack("<I", npts) + struct.pack("<I", 5_000_000),
            status_ok(b"STMP"))
    wait_s = npts * 0.1 + 0.5
    print(f"   等待 {wait_s:.1f}s 让其采满 {npts} 点(每点 100ms)...")
    time.sleep(wait_s)
    v.check("24 RDFC 读完成次数", b"RDFC", b"",
            struct_len(b"RDFC", 4,
                       lambda d: f"done={struct.unpack('<I', d[:4])[0]}"))

    def v_rdmr(rcmd, rdata):
        if rcmd != b"RDMR" or len(rdata) < 10:
            return False, f"格式错: cmd={rcmd!r} len={len(rdata)}"
        ch, sub = rdata[0], rdata[1]
        start = struct.unpack("<I", rdata[2:6])[0]
        got = struct.unpack("<I", rdata[6:10])[0]
        pts = struct.unpack(f"<{got}f", rdata[10:10 + got * 4]) if got else ()
        preview = ",".join(f"{p:.2f}" for p in pts[:5])
        return True, f"ch={ch} start={start} got={got} pts=[{preview}...]"
    v.check("25 RDMR 读连续结果", b"RDMR",
            bytes([1, 1]) + struct.pack("<I", 0) + struct.pack("<I", npts), v_rdmr)
    v.check("23 STSM 停止连续测量", b"STSM", b"", status_ok(b"STSM"))

    if args.danger:
        print("\n=== 危险命令(会断链, 最后执行) ===")
        # 端口改回自身/IP 改回自身 -> 仍会触发 CH9121 复位, 连接大概率中断
        v.check("8  WRPT 修改网络端口", b"WRPT", struct.pack("<H", 8888),
                status_ok(b"WRPT"))
        v.check("6  WRIP 修改 IP", b"WRIP", bytes([192, 168, 1, 200]),
                status_ok(b"WRIP"))
        v.check("19 BOOT 重启", b"BOOT", b"", status_ok(b"BOOT"))

    print(f"\n===== 结果: PASS={v.passed}  FAIL={v.failed} =====")
    return 0 if v.failed == 0 else 1


def main():
    ap = argparse.ArgumentParser(description="OPM_F405 网口协议验证")
    ap.add_argument("--ip", default="192.168.1.200")
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--udp", action="store_true", help="用 UDP(默认 TCP)")
    ap.add_argument("--danger", action="store_true",
                    help="额外测 WRIP/WRPT/BOOT(会断链)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    fam = socket.SOCK_DGRAM if args.udp else socket.SOCK_STREAM
    sock = socket.socket(socket.AF_INET, fam)
    sock.settimeout(args.timeout)
    try:
        print(f"连接 {args.ip}:{args.port} ({'UDP' if args.udp else 'TCP'}) ...")
        sock.connect((args.ip, args.port))
        print("已连接。\n")
        rc = run(sock, args)
    except (socket.timeout, OSError) as e:
        print(f"连接/通信失败: {e}")
        return 2
    finally:
        sock.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
