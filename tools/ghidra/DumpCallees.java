import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

// Usage: -postScript DumpCallees.java <convars.tsv> <outDir> <lo> <hi> <index.txt>...
// Decompiles direct callees (within [lo,hi)) of every function listed in the given index files,
// skipping ones already listed. Same cv_/rdata-float annotation as DumpConVarUsers.
public class DumpCallees extends GhidraScript {
    static final int CV_SIZE = 0x90;

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[1]);
        out.mkdirs();
        // lo == "self": dump the listed functions themselves instead of their callees
        boolean self = a[2].equals("self");
        long lo = self ? 0 : Long.decode(a[2]), hi = self ? 0 : Long.decode(a[3]);
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();

        TreeMap<Long, String> cvs = new TreeMap<>();
        Matcher cm = Pattern.compile("DAT_([0-9a-f]+),\"([^\"]+)\"").matcher("");
        for (String l : Files.readAllLines(Paths.get(a[0]))) {
            if (cm.reset(l).find()) cvs.put(Long.parseUnsignedLong(cm.group(1), 16), cm.group(2));
        }

        Set<Long> have = new HashSet<>();
        List<Function> roots = new ArrayList<>();
        for (int i = 4; i < a.length; i++) {
            for (String l : Files.readAllLines(Paths.get(a[i]))) {
                long ad = Long.parseUnsignedLong(l.split("\t")[0], 16);
                have.add(ad);
                Function f = getFunctionAt(sp.getAddress(ad));
                if (f != null) roots.add(f);
            }
        }
        TreeSet<Function> todo = new TreeSet<>(Comparator.comparing(Function::getEntryPoint));
        if (self) todo.addAll(roots);
        else for (Function f : roots) {
            for (Function c : f.getCalledFunctions(monitor)) {
                long e = c.getEntryPoint().getOffset();
                if (e >= lo && e < hi && !have.contains(e) && !c.isThunk()) todo.add(c);
            }
        }
        println("callees: " + todo.size());

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Pattern dat = Pattern.compile("DAT_([0-9a-f]{9,16})");
        try (PrintWriter idx = new PrintWriter(new File(out, "index.txt"))) {
            for (Function f : todo) {
                DecompileResults res = di.decompileFunction(f, 180, monitor);
                String c = res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage();
                Matcher m = dat.matcher(c);
                StringBuffer sb = new StringBuffer();
                Set<String> used = new TreeSet<>();
                while (m.find()) {
                    long ad = Long.parseUnsignedLong(m.group(1), 16);
                    Map.Entry<Long, String> fl = cvs.floorEntry(ad);
                    String rep = m.group();
                    if (fl != null && ad - fl.getKey() < CV_SIZE) {
                        long off = ad - fl.getKey();
                        rep = "cv_" + fl.getValue() + (off == 0 ? "" : "_0x" + Long.toHexString(off));
                        used.add(fl.getValue());
                    } else {
                        Address cad = sp.getAddress(ad);
                        MemoryBlock b = currentProgram.getMemory().getBlock(cad);
                        if (b != null && b.getName().equals(".rdata")) {
                            try {
                                float fv = Float.intBitsToFloat(currentProgram.getMemory().getInt(cad));
                                double dv = Double.longBitsToDouble(currentProgram.getMemory().getLong(cad));
                                rep = m.group() + "/*f=" + fv + (Math.abs(dv) > 1e-6 && Math.abs(dv) < 1e9 ? " d=" + dv : "") + "*/";
                            } catch (Exception ex) { }
                        }
                    }
                    m.appendReplacement(sb, Matcher.quoteReplacement(rep));
                }
                m.appendTail(sb);
                try (PrintWriter w = new PrintWriter(new File(out, "fn_" + f.getEntryPoint() + ".c"))) { w.println(sb); }
                idx.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + String.join(",", used));
            }
        }
    }
}
