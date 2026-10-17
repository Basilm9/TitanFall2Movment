import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;

// Usage: -postScript RelScan8.java <outFile> <target> ...  (hex)
// Brute-force: every 4-byte little-endian value v at .text offset p such that p+4+k+v == target (k = 0..4,
// i.e. rel32 followed by 0-4 bytes of immediate). Reports whether p lies inside a disassembled instruction.
public class RelScan8 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        Listing lst = currentProgram.getListing();
        MemoryBlock text = mem.getBlock(".text");
        long s = text.getStart().getOffset();
        int n = (int) text.getSize();
        byte[] buf = new byte[n];
        text.getBytes(text.getStart(), buf);
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                long t = Long.parseUnsignedLong(a[i], 16);
                w.println("== " + a[i]);
                for (int p = 0; p + 4 <= n; p++) {
                    int v = (buf[p] & 0xff) | (buf[p + 1] & 0xff) << 8 | (buf[p + 2] & 0xff) << 16 | (buf[p + 3] & 0xff) << 24;
                    for (int k = 0; k <= 4; k++) {
                        if (s + p + 4 + k + v == t) {
                            Address ad = sp.getAddress(s + p);
                            Instruction ins = lst.getInstructionContaining(ad);
                            Function f = getFunctionContaining(ad);
                            w.println("  " + ad + " k=" + k + " prev=" + String.format("%02x", p > 0 ? buf[p - 1] & 0xff : 0) + " ins=" + (ins == null ? "NONE" : ins.getAddress() + " " + ins) + " fn=" + (f == null ? "-" : f.getEntryPoint().toString()));
                        }
                    }
                }
            }
        }
    }
}
