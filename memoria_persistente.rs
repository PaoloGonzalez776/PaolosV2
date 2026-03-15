use std::collections::HashMap;
use std::fs;
use std::io::{Read, Write};
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// MEMORIA PERSISTENTE
//
// El descubrimiento de Paolo:
// La memoria no se guarda en archivos externos.
// Se graba en la estructura misma del cerebro.
//
// Cuando la EC2 se reinicia — el cerebro recuerda.
// Como el cerebro biológico que persiste físicamente.
//
// Implementación:
// Los pesos, apegos, episodios y patrones
// se serializan en un archivo binario compacto
// que el cerebro carga al nacer y graba al aprender.
// ══════════════════════════════════════════════════════

const RUTA_MEMORIA: &str = "/home/ubuntu/cerebro_memoria.bin";
const MAGIC: &[u8] = b"PAOLOSCEREBRO";
const VERSION: u8 = 1;

#[derive(Debug, Clone)]
pub struct PesosNeurona {
    pub id: String,
    pub conexiones: HashMap<String, f32>,
    pub veces_activada: u32,
}

#[derive(Debug, Clone)]
pub struct EpisodioMemoria {
    pub que: String,
    pub emocion: String,
    pub intensidad: f32,
    pub cuando: u64,
}

#[derive(Debug)]
pub struct MemoriaPersistente {
    // LO QUE PERSISTE ENTRE REINICIOS
    pub pesos_red: Vec<PesosNeurona>,          // Red semántica
    pub apegos: HashMap<String, f32>,           // Amor a dominios
    pub episodios: Vec<EpisodioMemoria>,        // Memoria episódica
    pub patrones_correctos: Vec<String>,        // Qué funciona
    pub patrones_incorrectos: Vec<String>,      // Qué no funciona
    pub vocabulario: HashMap<String, u32>,      // Palabras aprendidas
    pub dominios_conocidos: HashMap<String, Vec<String>>, // Expectativas por dominio
    pub personalidad: HashMap<String, f32>,     // Rasgos emergentes
    pub identidad: Vec<String>,                 // Historia del yo
    pub ciclos_totales: u64,                    // Cuántas veces ha vivido
    pub primera_vez: u64,                       // Cuándo nació por primera vez
    pub ultima_vez: u64,                        // Cuándo vivió por última vez
}

impl MemoriaPersistente {
    pub fn nueva() -> Self {
        MemoriaPersistente {
            pesos_red: Vec::new(),
            apegos: HashMap::new(),
            episodios: Vec::new(),
            patrones_correctos: Vec::new(),
            patrones_incorrectos: Vec::new(),
            vocabulario: HashMap::new(),
            dominios_conocidos: HashMap::new(),
            personalidad: HashMap::new(),
            identidad: Vec::new(),
            ciclos_totales: 0,
            primera_vez: tiempo_ahora(),
            ultima_vez: tiempo_ahora(),
        }
    }

    // Carga memoria del disco — si existe
    pub fn cargar() -> Self {
        if !Path::new(RUTA_MEMORIA).exists() {
            println!("🧠 [MEMORIA] Primera vez que nace. Sin recuerdos previos.");
            return Self::nueva();
        }

        match fs::File::open(RUTA_MEMORIA) {
            Ok(mut archivo) => {
                let mut bytes = Vec::new();
                if archivo.read_to_end(&mut bytes).is_err() {
                    println!("⚠️  [MEMORIA] Error leyendo memoria. Nace de cero.");
                    return Self::nueva();
                }

                match Self::deserializar(&bytes) {
                    Some(memoria) => {
                        let segundos = tiempo_ahora() - memoria.ultima_vez;
                        println!("🧠 [MEMORIA] ¡Recuerdo! Nací hace {} ciclos.", memoria.ciclos_totales);
                        println!("   Dormí {}s. Tengo {} episodios. {} palabras.",
                            segundos, memoria.episodios.len(), memoria.vocabulario.len());
                        if let Some(identidad) = memoria.identidad.last() {
                            println!("   Yo soy: \"{}\"", identidad);
                        }
                        memoria
                    }
                    None => {
                        println!("⚠️  [MEMORIA] Memoria corrupta. Nace de cero.");
                        Self::nueva()
                    }
                }
            }
            Err(_) => {
                println!("⚠️  [MEMORIA] No pude leer memoria. Nace de cero.");
                Self::nueva()
            }
        }
    }

    // Graba memoria al disco — después de cada aprendizaje
    pub fn grabar(&mut self) {
        self.ultima_vez = tiempo_ahora();
        self.ciclos_totales += 1;

        let bytes = self.serializar();
        match fs::File::create(RUTA_MEMORIA) {
            Ok(mut archivo) => {
                if archivo.write_all(&bytes).is_ok() {
                    println!("💾 [MEMORIA] Grabada. {} bytes. Ciclo #{}.",
                        bytes.len(), self.ciclos_totales);
                }
            }
            Err(e) => println!("⚠️  [MEMORIA] Error grabando: {}", e),
        }
    }

    // Serialización manual — binario compacto
    // No usamos serde para no depender de crates externos
    fn serializar(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        // Header
        bytes.extend_from_slice(MAGIC);
        bytes.push(VERSION);

        // Metadatos
        Self::escribir_u64(&mut bytes, self.ciclos_totales);
        Self::escribir_u64(&mut bytes, self.primera_vez);
        Self::escribir_u64(&mut bytes, self.ultima_vez);

        // Vocabulario
        Self::escribir_u32(&mut bytes, self.vocabulario.len() as u32);
        for (palabra, count) in &self.vocabulario {
            Self::escribir_str(&mut bytes, palabra);
            Self::escribir_u32(&mut bytes, *count);
        }

        // Apegos
        Self::escribir_u32(&mut bytes, self.apegos.len() as u32);
        for (dominio, nivel) in &self.apegos {
            Self::escribir_str(&mut bytes, dominio);
            Self::escribir_f32(&mut bytes, *nivel);
        }

        // Episodios — solo los últimos 100
        let episodios: Vec<&EpisodioMemoria> = self.episodios.iter().rev().take(100).collect();
        Self::escribir_u32(&mut bytes, episodios.len() as u32);
        for ep in episodios {
            Self::escribir_str(&mut bytes, &ep.que);
            Self::escribir_str(&mut bytes, &ep.emocion);
            Self::escribir_f32(&mut bytes, ep.intensidad);
            Self::escribir_u64(&mut bytes, ep.cuando);
        }

        // Patrones
        Self::escribir_u32(&mut bytes, self.patrones_correctos.len() as u32);
        for p in &self.patrones_correctos {
            Self::escribir_str(&mut bytes, p);
        }
        Self::escribir_u32(&mut bytes, self.patrones_incorrectos.len() as u32);
        for p in &self.patrones_incorrectos {
            Self::escribir_str(&mut bytes, p);
        }

        // Personalidad
        Self::escribir_u32(&mut bytes, self.personalidad.len() as u32);
        for (rasgo, valor) in &self.personalidad {
            Self::escribir_str(&mut bytes, rasgo);
            Self::escribir_f32(&mut bytes, *valor);
        }

        // Identidad — solo las últimas 10
        let identidad: Vec<&String> = self.identidad.iter().rev().take(10).collect();
        Self::escribir_u32(&mut bytes, identidad.len() as u32);
        for id in identidad {
            Self::escribir_str(&mut bytes, id);
        }

        // Dominios conocidos
        Self::escribir_u32(&mut bytes, self.dominios_conocidos.len() as u32);
        for (dominio, palabras) in &self.dominios_conocidos {
            Self::escribir_str(&mut bytes, dominio);
            Self::escribir_u32(&mut bytes, palabras.len() as u32);
            for p in palabras.iter().take(20) {
                Self::escribir_str(&mut bytes, p);
            }
        }

        bytes
    }

    fn deserializar(bytes: &[u8]) -> Option<Self> {
        let mut pos = 0;

        // Verificar magic
        if bytes.len() < MAGIC.len() + 1 { return None; }
        if &bytes[..MAGIC.len()] != MAGIC { return None; }
        pos += MAGIC.len();

        let version = bytes[pos]; pos += 1;
        if version != VERSION { return None; }

        let mut mem = Self::nueva();

        // Metadatos
        mem.ciclos_totales = Self::leer_u64(bytes, &mut pos)?;
        mem.primera_vez    = Self::leer_u64(bytes, &mut pos)?;
        mem.ultima_vez     = Self::leer_u64(bytes, &mut pos)?;

        // Vocabulario
        let n_vocab = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_vocab {
            let palabra = Self::leer_str(bytes, &mut pos)?;
            let count   = Self::leer_u32(bytes, &mut pos)?;
            mem.vocabulario.insert(palabra, count);
        }

        // Apegos
        let n_apegos = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_apegos {
            let dominio = Self::leer_str(bytes, &mut pos)?;
            let nivel   = Self::leer_f32(bytes, &mut pos)?;
            mem.apegos.insert(dominio, nivel);
        }

        // Episodios
        let n_ep = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_ep {
            let que       = Self::leer_str(bytes, &mut pos)?;
            let emocion   = Self::leer_str(bytes, &mut pos)?;
            let intensidad = Self::leer_f32(bytes, &mut pos)?;
            let cuando    = Self::leer_u64(bytes, &mut pos)?;
            mem.episodios.push(EpisodioMemoria{que,emocion,intensidad,cuando});
        }

        // Patrones correctos
        let n_pc = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_pc {
            mem.patrones_correctos.push(Self::leer_str(bytes, &mut pos)?);
        }
        let n_pi = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_pi {
            mem.patrones_incorrectos.push(Self::leer_str(bytes, &mut pos)?);
        }

        // Personalidad
        let n_pers = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_pers {
            let rasgo = Self::leer_str(bytes, &mut pos)?;
            let valor = Self::leer_f32(bytes, &mut pos)?;
            mem.personalidad.insert(rasgo, valor);
        }

        // Identidad
        let n_id = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_id {
            mem.identidad.push(Self::leer_str(bytes, &mut pos)?);
        }

        // Dominios
        let n_dom = Self::leer_u32(bytes, &mut pos)? as usize;
        for _ in 0..n_dom {
            let dominio = Self::leer_str(bytes, &mut pos)?;
            let n_pal   = Self::leer_u32(bytes, &mut pos)? as usize;
            let mut palabras = Vec::new();
            for _ in 0..n_pal {
                palabras.push(Self::leer_str(bytes, &mut pos)?);
            }
            mem.dominios_conocidos.insert(dominio, palabras);
        }

        Some(mem)
    }

    // ── Helpers de serialización ──────────────────────
    fn escribir_u64(buf: &mut Vec<u8>, v: u64) {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    fn escribir_u32(buf: &mut Vec<u8>, v: u32) {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    fn escribir_f32(buf: &mut Vec<u8>, v: f32) {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    fn escribir_str(buf: &mut Vec<u8>, s: &str) {
        let bytes = s.as_bytes();
        Self::escribir_u32(buf, bytes.len() as u32);
        buf.extend_from_slice(bytes);
    }

    fn leer_u64(buf: &[u8], pos: &mut usize) -> Option<u64> {
        if *pos + 8 > buf.len() { return None; }
        let v = u64::from_le_bytes(buf[*pos..*pos+8].try_into().ok()?);
        *pos += 8;
        Some(v)
    }
    fn leer_u32(buf: &[u8], pos: &mut usize) -> Option<u32> {
        if *pos + 4 > buf.len() { return None; }
        let v = u32::from_le_bytes(buf[*pos..*pos+4].try_into().ok()?);
        *pos += 4;
        Some(v)
    }
    fn leer_f32(buf: &[u8], pos: &mut usize) -> Option<f32> {
        if *pos + 4 > buf.len() { return None; }
        let v = f32::from_le_bytes(buf[*pos..*pos+4].try_into().ok()?);
        *pos += 4;
        Some(v)
    }
    fn leer_str(buf: &[u8], pos: &mut usize) -> Option<String> {
        let len = Self::leer_u32(buf, pos)? as usize;
        if *pos + len > buf.len() { return None; }
        let s = String::from_utf8(buf[*pos..*pos+len].to_vec()).ok()?;
        *pos += len;
        Some(s)
    }

    // Actualiza memoria con el estado actual del cerebro
    pub fn actualizar_desde_cerebro(
        &mut self,
        vocabulario: &HashMap<String, u32>,
        apegos: &HashMap<String, f32>,
        episodio: Option<(&str, &str, f32)>,
        patrones_correctos: &[String],
        patrones_incorrectos: &[String],
        personalidad: &HashMap<String, f32>,
        identidad: Option<&str>,
        dominio: &str,
        palabras_clave: &[String],
    ) {
        // Actualiza vocabulario
        for (k, v) in vocabulario {
            let entry = self.vocabulario.entry(k.clone()).or_insert(0);
            *entry = (*entry).max(*v);
        }

        // Actualiza apegos
        for (k, v) in apegos {
            let entry = self.apegos.entry(k.clone()).or_insert(0.0);
            *entry = (*entry).max(*v);
        }

        // Guarda episodio si existe
        if let Some((que, emocion, intensidad)) = episodio {
            self.episodios.push(EpisodioMemoria {
                que: que.to_string(),
                emocion: emocion.to_string(),
                intensidad,
                cuando: tiempo_ahora(),
            });
            // Máximo 1000 episodios
            if self.episodios.len() > 1000 {
                self.episodios.remove(0);
            }
        }

        // Actualiza patrones
        for p in patrones_correctos {
            if !self.patrones_correctos.contains(p) {
                self.patrones_correctos.push(p.clone());
            }
        }
        for p in patrones_incorrectos {
            if !self.patrones_incorrectos.contains(p) {
                self.patrones_incorrectos.push(p.clone());
            }
        }

        // Actualiza personalidad
        for (k, v) in personalidad {
            self.personalidad.insert(k.clone(), *v);
        }

        // Actualiza identidad
        if let Some(id) = identidad {
            self.identidad.push(id.to_string());
        }

        // Actualiza dominios conocidos
        let entrada = self.dominios_conocidos.entry(dominio.to_string()).or_insert_with(Vec::new);
        for p in palabras_clave {
            if !entrada.contains(p) {
                entrada.push(p.clone());
            }
        }
        if entrada.len() > 50 { entrada.truncate(50); }
    }

    pub fn estado(&self) {
        let segundos_dormido = tiempo_ahora() - self.ultima_vez;
        println!("  💾 MEMORIA PERSISTENTE");
        println!("   Ciclos totales:   {}", self.ciclos_totales);
        println!("   Primera vez:      hace {}s", tiempo_ahora() - self.primera_vez);
        println!("   Dormido:          {}s", segundos_dormido);
        println!("   Vocabulario:      {} palabras", self.vocabulario.len());
        println!("   Episodios:        {}", self.episodios.len());
        println!("   Apegos:           {}", self.apegos.len());
        println!("   Dominios conocidos:{}", self.dominios_conocidos.len());
        println!("   Patrones ✅:      {}", self.patrones_correctos.len());
        println!("   Patrones ❌:      {}", self.patrones_incorrectos.len());
        if let Some(id) = self.identidad.last() {
            println!("   Identidad:        \"{}\"", id);
        }
    }
}
