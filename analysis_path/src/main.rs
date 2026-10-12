use anyhow::{bail, Context, Result};
use lender::*;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::time::Instant;
use webgraph::prelude::*;

const UNSET: u32 = u32::MAX;

fn write_u32_le(path: &str, data: &[u32]) -> Result<()> {
    let mut w = BufWriter::with_capacity(1 << 20, File::create(path)?);
    for x in data {
        w.write_all(&x.to_le_bytes())?;
    }
    w.flush()?;
    Ok(())
}

fn main() -> Result<()> {
    let t_total = Instant::now();

    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        bail!("uso: {} <basename-grafo> <prefisso-output>", args[0]);
    }
    let basename = &args[1];
    let out_prefix = &args[2];

    // Il grafo da passare e' quello ORIGINALE (non il trasposto):
    // la propagazione va dai nodi ai loro successori.
    let t_load = Instant::now();
    let graph = BvGraphSeq::with_basename(basename)
        .load()
        .context("caricamento del grafo")?;
    let n = graph.num_nodes();
    println!("caricamento grafo: {:.3} s", t_load.elapsed().as_secs_f64());

    // d_max / d_min e, per ciascun nodo, la sorgente (coinbase) che li induce.
    let mut dmax = vec![0u32; n];
    let mut dmin = vec![UNSET; n];
    let mut srcmax = vec![UNSET; n];
    let mut srcmin = vec![UNSET; n];

    let mut num_sources = 0usize;
    let mut num_arcs = 0u64;

    let t_scan = Instant::now();
    let mut iter = graph.iter();
    while let Some((u, succs)) = iter.next() {
        // Nessun predecessore ha raggiunto u: u e' una sorgente (coinbase).
        if dmin[u] == UNSET {
            dmin[u] = 0;
            srcmin[u] = u as u32;
            srcmax[u] = u as u32;
            num_sources += 1;
        }
        let (du_max, du_min) = (dmax[u] + 1, dmin[u] + 1);
        let (su_max, su_min) = (srcmax[u], srcmin[u]);

        for v in succs {
            num_arcs += 1;
            if v <= u {
                bail!(
                    "arco ({u},{v}) viola l'ordine topologico: \
                     gli ID devono essere cronologici (u < v)"
                );
            }
            // A parita' di profondita' si tiene la prima sorgente incontrata
            // (confronto stretto), cioe' quella con l'ID piu' basso fra i predecessori
            // visitati per primi.
            if du_max > dmax[v] {
                dmax[v] = du_max;
                srcmax[v] = su_max;
            }
            if du_min < dmin[v] {
                dmin[v] = du_min;
                srcmin[v] = su_min;
            }
        }
    }
    println!("scansione e calcolo: {:.3} s", t_scan.elapsed().as_secs_f64());

    let t_write = Instant::now();
    write_u32_le(&format!("{out_prefix}.dmax"), &dmax)?;
    write_u32_le(&format!("{out_prefix}.dmin"), &dmin)?;
    write_u32_le(&format!("{out_prefix}.srcmax"), &srcmax)?;
    write_u32_le(&format!("{out_prefix}.srcmin"), &srcmin)?;
    println!("scrittura output: {:.3} s", t_write.elapsed().as_secs_f64());

    let gmax = dmax.iter().copied().max().unwrap_or(0);
    let argmax = dmax.iter().position(|&d| d == gmax).unwrap_or(0);
    println!("nodi:            {n}");
    println!("archi:           {num_arcs}");
    println!("sorgenti:        {num_sources}");
    println!(
        "d_max globale:   {gmax} (nodo {argmax}, sorgente {})",
        srcmax[argmax]
    );
    println!(
        "d_max medio:     {:.3}",
        dmax.iter().map(|&d| d as f64).sum::<f64>() / n as f64
    );
    println!(
        "d_min medio:     {:.3}",
        dmin.iter().map(|&d| d as f64).sum::<f64>() / n as f64
    );

    println!("tempo totale:    {:.3} s", t_total.elapsed().as_secs_f64());
    Ok(())
}