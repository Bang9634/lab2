use std::io::{self, Write};

fn read_line(prompt: &str) -> Result<String, String> {
    print!("{prompt}");
    io::stdout().flush().map_err(|e| e.to_string())?;
    let mut line = String::new();
    if io::stdin().read_line(&mut line).map_err(|e| e.to_string())? == 0 {
        return Err("unexpected end of input".into());
    }
    Ok(line)
}

fn dimensions(label: &str) -> Result<(usize, usize), String> {
    let line = read_line(&format!("{label} rows cols: "))?;
    let values = line
        .split_whitespace()
        .map(str::parse::<usize>)
        .collect::<Result<Vec<_>, _>>()
        .map_err(|_| format!("{label}: dimensions must be positive integers"))?;
    match values.as_slice() {
        [rows, cols] if *rows > 0 && *cols > 0 => Ok((*rows, *cols)),
        _ => Err(format!("{label}: enter two positive dimensions")),
    }
}

fn read_matrix(label: &str, rows: usize, cols: usize) -> Result<Vec<i64>, String> {
    let count = rows.checked_mul(cols).ok_or("matrix size overflow")?;
    let mut values = Vec::new();
    values.try_reserve_exact(count).map_err(|e| e.to_string())?;
    for row in 1..=rows {
        let line = read_line(&format!("{label} row {row} ({cols} integers): "))?;
        let start = values.len();
        for field in line.split_whitespace() {
            if values.len() - start == cols {
                return Err(format!("{label} row {row}: expected {cols} values"));
            }
            values.push(field.parse::<i64>().map_err(|_| format!("invalid integer: {field}"))?);
        }
        if values.len() - start != cols {
            return Err(format!("{label} row {row}: expected {cols} values"));
        }
    }
    Ok(values)
}

fn run() -> Result<(), String> {
    let (rows, cols) = dimensions("A")?;
    if dimensions("B")? != (rows, cols) {
        return Err("A and B must have the same dimensions".into());
    }
    let mut a = read_matrix("A", rows, cols)?;
    let b = read_matrix("B", rows, cols)?;
    for (left, right) in a.iter_mut().zip(&b) {
        *left = left.checked_add(*right).ok_or("matrix element overflow")?;
    }
    println!("A + B:");
    for row in a.chunks_exact(cols) {
        let line = row.iter().map(i64::to_string).collect::<Vec<_>>().join(" ");
        println!("{line}");
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("matrix_add: {error}");
        std::process::exit(1);
    }
}
