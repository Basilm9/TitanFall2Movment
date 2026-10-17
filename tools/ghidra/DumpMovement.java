import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

// Usage: -postScript DumpMovement.java <outDir> <regex over string values>
// For each defined string matching the regex, finds functions referencing it and decompiles them (deduped).
public class DumpMovement extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]);
        out.mkdirs();
        // a[1] is a file holding the regex (cmd.exe mangles '|' in arguments)
        String re = new String(java.nio.file.Files.readAllBytes(new File(a[1]).toPath())).trim();
        java.util.regex.Pattern pat = java.util.regex.Pattern.compile(re, java.util.regex.Pattern.CASE_INSENSITIVE);

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Map<Address, List<String>> fnToStrings = new TreeMap<>();

        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            if (!pat.matcher(s).find()) continue;
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
            while (refs.hasNext()) {
                Reference r = refs.next();
                Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                fnToStrings.computeIfAbsent(f.getEntryPoint(), k -> new ArrayList<>()).add(s);
            }
        }
        println("functions: " + fnToStrings.size());

        try (PrintWriter idx = new PrintWriter(new File(out, "index.txt"))) {
            for (Map.Entry<Address, List<String>> e : fnToStrings.entrySet()) {
                Function f = currentProgram.getFunctionManager().getFunctionAt(e.getKey());
                DecompileResults res = di.decompileFunction(f, 120, monitor);
                String code = res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage();
                String name = "fn_" + e.getKey().toString();
                try (PrintWriter w = new PrintWriter(new File(out, name + ".c"))) { w.println(code); }
                idx.println(e.getKey() + "\t" + f.getBody().getNumAddresses() + "\t" + String.join(",", new LinkedHashSet<>(e.getValue())));
            }
        }
    }
}
