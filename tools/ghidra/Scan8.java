import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.lang.Register;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

// Usage: -postScript Scan8.java <outDir> <targets comma-separated hex> <displacements comma-separated hex>
// 1) refs.txt: every reference (code + data) to each target, plus raw 8-byte pointer hits in memory
// 2) disp.txt: every instruction with a memory operand [reg + disp] whose disp is in the list, float/xmm ops flagged
public class Scan8 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]);
        out.mkdirs();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing lst = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new File(out, "refs.txt"))) {
            for (String t : a[1].split("\\+")) {
                long tv = Long.parseUnsignedLong(t, 16);
                Address ta = sp.getAddress(tv);
                w.println("== target " + t);
                for (Reference r : rm.getReferencesTo(ta)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    Instruction ins = lst.getInstructionAt(r.getFromAddress());
                    w.println("  ref " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()) + " : " + (ins == null ? "" : ins.toString()));
                }
                byte[] pat = new byte[8];
                for (int i = 0; i < 8; i++) pat[i] = (byte) (tv >>> (8 * i));
                for (MemoryBlock b : mem.getBlocks()) {
                    if (!b.isInitialized()) continue;
                    Address s = b.getStart();
                    while (s != null && s.compareTo(b.getEnd()) <= 0) {
                        Address h = mem.findBytes(s, b.getEnd(), pat, null, true, monitor);
                        if (h == null) break;
                        w.println("  ptr " + h + " in block " + b.getName());
                        s = h.add(1);
                    }
                }
            }
        }

        Set<Long> disps = new HashSet<>();
        for (String d : a[2].split("\\+")) disps.add(Long.parseUnsignedLong(d, 16));
        try (PrintWriter w = new PrintWriter(new File(out, "disp.txt"))) {
            InstructionIterator it = lst.getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    int ty = ins.getOperandType(op);
                    if ((ty & OperandType.DYNAMIC) == 0 && (ty & OperandType.ADDRESS) == 0) continue;
                    Object[] objs = ins.getOpObjects(op);
                    boolean hasReg = false; Long hit = null; String regs = "";
                    for (Object o : objs) {
                        if (o instanceof Register) { hasReg = true; regs += ((Register) o).getName() + " "; }
                        if (o instanceof Scalar && disps.contains(((Scalar) o).getUnsignedValue())) hit = ((Scalar) o).getUnsignedValue();
                    }
                    if (!hasReg || hit == null) continue;
                    if (regs.contains("RSP") || regs.contains("RBP") && false) continue;
                    Function f = getFunctionContaining(ins.getAddress());
                    String s = ins.toString();
                    boolean flt = s.contains("XMM") || s.startsWith("MOVSS") || s.startsWith("COMISS") || s.startsWith("UCOMISS");
                    w.println(Long.toHexString(hit) + "\t" + ins.getAddress() + "\t" + (f == null ? "?" : f.getEntryPoint().toString()) + "\t" + (flt ? "F" : "-") + "\t" + s);
                }
            }
        }
    }
}

