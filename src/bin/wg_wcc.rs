// wg_wcc: calcola le weakly connected components di un grafo WebGraph.
//
// Le componenti debolmente connesse ignorano la direzione degli archi: due
// nodi sono nella stessa componente se esiste un cammino fra loro
// ignorando il verso degli archi. Per calcolarle non serve simmetrizzare
// il grafo (webgraph transform simplify) ne' costruire il trasposto: basta
// unire gli estremi di ogni arco cosi' come compare nella scansione
// originale, con una union-find. Questo evita sia il preprocessing sia il
// bisogno dell'indice .ef per l'accesso casuale: una singola scansione
// sequenziale del grafo (BvGraphSeq) e' sufficiente.
//
// Uso:
//   wg_wcc <basename> <output.tsv>
//
// Il file di output contiene una riga per ogni nodo del grafo: la riga
// i-esima (a partire da 0) contiene solo l'id della componente a cui
// appartiene il nodo i-esimo. Gli id di componente sono rinumerati da 0 a
// k-1 nell'ordine in cui le radici vengono incontrate durante la seconda
// scansione (0..num_nodes).

use anyhow::{Context, Result};
use clap::Parser;
use lender::prelude::*;
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::PathBuf;
use webgraph::prelude::*;

#[derive(Parser, Debug)]
struct Args {
    /// Basename del grafo (senza estensione).
    basename: PathBuf,
    /// File di output: una riga per nodo, con il solo id di componente
    /// (la riga i-esima si riferisce al nodo i-esimo).
    output: PathBuf,
}

/// Union-find con path halving (iterativo, non ricorsivo: su grafi con
/// miliardi di nodi una find() ricorsiva rischierebbe lo stack overflow)
/// e union by rank.
struct UnionFind {
    parent: Vec<usize>,
    rank: Vec<u8>,
}

impl UnionFind {
    fn new(n: usize) -> Self {
        UnionFind {
            parent: (0..n).collect(),
            rank: vec![0u8; n],
        }
    }

    fn find(&mut self, mut x: usize) -> usize {
        while self.parent[x] != x {
            // Path halving: dimezza il cammino verso la radice ad ogni
            // passo, senza bisogno di una seconda passata come nel path
            // compression classico.
            self.parent[x] = self.parent[self.parent[x]];
            x = self.parent[x];
        }
        x
    }

    fn union(&mut self, a: usize, b: usize) {
        let ra = self.find(a);
        let rb = self.find(b);
        if ra == rb {
            return;
        }
        match self.rank[ra].cmp(&self.rank[rb]) {
            std::cmp::Ordering::Less => self.parent[ra] = rb,
            std::cmp::Ordering::Greater => self.parent[rb] = ra,
            std::cmp::Ordering::Equal => {
                self.parent[rb] = ra;
                self.rank[ra] = self.rank[ra].saturating_add(1);
            }
        }
    }
}

fn main() -> Result<()> {
    let args = Args::parse();

    // Caricamento sequenziale: una sola scansione degli archi, non serve
    // l'indice .ef ne' l'accesso casuale.
    let graph = BvGraphSeq::with_basename(&args.basename)
        .load()
        .with_context(|| format!("caricamento del grafo {}", args.basename.display()))?;

    let num_nodes = graph.num_nodes();
    let mut uf = UnionFind::new(num_nodes);

    // Prima passata: unisce gli estremi di ogni arco, ignorando la
    // direzione. Non serve leggere anche il trasposto: (u, v) e (v, u)
    // portano alla stessa union, quindi scorrere solo gli archi diretti
    // cosi' come sono nel grafo basta a ottenere le componenti corrette.
    for_!((node, succs) in graph.iter() {
        for succ in succs {
            uf.union(node, succ);
        }
    });

    // Seconda passata: rinumera le radici in id di componente consecutivi
    // (0..k-1), nell'ordine in cui vengono incontrate. Uso una HashMap
    // invece di un vettore grande quanto num_nodes perche' il numero di
    // componenti distinte e' tipicamente molto piu' piccolo del numero di
    // nodi (un grafo reale ha in genere una componente gigante e poche
    // altre piccole).
    let mut component_of_root: HashMap<usize, usize> = HashMap::new();
    let mut component = vec![0usize; num_nodes];
    for node in 0..num_nodes {
        let root = uf.find(node);
        let next_id = component_of_root.len();
        let id = *component_of_root.entry(root).or_insert(next_id);
        component[node] = id;
    }
    let num_components = component_of_root.len();

    let out_file = File::create(&args.output)
        .with_context(|| format!("creazione del file di output {}", args.output.display()))?;
    let mut out = BufWriter::with_capacity(1 << 20, out_file);
    for node in 0..num_nodes {
        writeln!(out, "{}", component[node])?;
    }
    out.flush()?;

    eprintln!(
        "Nodi: {}\tComponenti debolmente connesse: {}",
        num_nodes, num_components
    );
    Ok(())
}
