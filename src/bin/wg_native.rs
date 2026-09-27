// wg_native: costruisce un BvGraph direttamente da una edge list binaria
// gia' ordinata (per source, poi per target) e deduplicata, come quella
// prodotta da pg_el_builder + edge_sorter.
//
// A differenza di `webgraph from arcs`, questo programma NON passa dal
// testo e NON rifa' un ordinamento esterno: legge la edge list via mmap,
// la reinterpreta a costo zero come coppie di u64 (byte order nativo,
// coerente con quello che scrive pg_el_builder) e la incapsula
// direttamente in un ArcListGraph, saltando SortPairs.
//
// Uso:
//   wg_native <edges.bin> <basename_output> <num_nodes>
//
// <num_nodes> deve essere il valore "Nodes: X" stampato da pg_el_builder,
// non un valore dedotto dagli archi: alcuni nodi (le UTXO mai spese) non
// compaiono mai come sorgente ne' come destinazione di nessun arco, quindi
// dedurlo dal flusso di archi lo sottostimerebbe.

use anyhow::{Context, Result};
use memmap2::Mmap;
use std::fs::File;
use std::path::PathBuf;
use webgraph::graphs::arc_list_graph::ArcListGraph;
use webgraph::prelude::*;

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 4 {
        anyhow::bail!("Usage: {} <edges.bin> <basename_output> <num_nodes>", args[0]);
    }
    let input_path = &args[1];
    let dst = PathBuf::from(&args[2]);
    let num_nodes: usize = args[3].parse().context("num_nodes non valido")?;

    // Mappa il file in memoria: nessuna copia, il kernel pagina i dati on
    // demand seguendo l'accesso sequenziale che facciamo sotto.
    let file = File::open(input_path).context("apertura del file di input")?;
    let mmap = unsafe { Mmap::map(&file).context("mmap del file di input")? };

    if mmap.len() % 16 != 0 {
        anyhow::bail!(
            "la dimensione del file ({} byte) non e' un multiplo di 16",
            mmap.len()
        );
    }
    let num_arcs = mmap.len() / 16;

    // Reinterpreta il buffer mappato come una sequenza di coppie (u64, u64)
    // in byte order nativo: e' esattamente il formato scritto da
    // EdgeWriter/EdgeSink in pg_el_builder ed edge_sorter, quindi questo
    // cast e' valido fintanto che si esegue sulla stessa architettura che
    // ha generato il file. mmap restituisce sempre un indirizzo allineato
    // a pagina, quindi l'allineamento a 8 byte richiesto da u64 e' garantito.
    let ptr = mmap.as_ptr() as *const (u64, u64);
    let pairs: &[(u64, u64)] = unsafe { std::slice::from_raw_parts(ptr, num_arcs) };

    // Iteratore pigro: nessuna allocazione, nessun parsing testuale.
    // I dati sono gia' ordinati e deduplicati da edge_sorter, quindi non
    // serve ne' un ulteriore sort ne' un .dedup().
    let arcs = pairs.iter().map(|&(s, t)| (s as usize, t as usize));

    let g = ArcListGraph::new(num_nodes, arcs);

    let tmp_dir = tempfile::Builder::new()
        .prefix("wg_native_")
        .tempdir()
        .context("creazione della directory temporanea")?;

    let mut builder = BvCompConfig::new(&dst)
        .with_comp_flags(CompFlags::default())
        .with_tmp_dir(tmp_dir.path());

    builder
        .par_comp_lenders_endianness(&g, num_nodes, "big")
        .context("compressione del grafo")?;

    println!("Grafo compresso: {} nodi, {} archi -> {}", num_nodes, num_arcs, dst.display());
    Ok(())
}
