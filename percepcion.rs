use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};
use std::fs;

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// PERCEPCIÓN COMPLETA v1.0
//
// Vista mejorada + Tiempo + Propioceptión
//
// El cerebro siente su propio cuerpo (EC2)
// CPU alta   → cansado
// RAM llena  → saturado
// Red lenta  → lento
// ══════════════════════════════════════════════════════

// ── PROPIOCEPTIÓN — Siente su propio cuerpo ──────────
#[derive(Debug, Clone)]
pub struct EstadoCorporal {
    pub cpu_uso:      f32,   // 0.0 a 1.0
    pub ram_uso:      f32,   // 0.0 a 1.0
    pub ram_libre_mb: f32,
    pub carga_sistema: f32,  // load average
    pub sensacion:    String, // cómo se siente
    pub cuando:       u64,
}

impl EstadoCorporal {
    pub fn leer() -> Self {
        // Lee CPU desde /proc/stat
        let cpu_uso = Self::leer_cpu();
        
        // Lee RAM desde /proc/meminfo
        let (ram_uso, ram_libre_mb) = Self::leer_ram();
        
        // Lee carga del sistema
        let carga = Self::leer_carga();

        // Determina cómo se siente
        let sensacion = Self::calcular_sensacion(cpu_uso, ram_uso, carga);

        EstadoCorporal {
            cpu_uso,
            ram_uso,
            ram_libre_mb,
            carga_sistema: carga,
            sensacion: sensacion.clone(),
            cuando: tiempo_ahora(),
        }
    }

    fn leer_cpu() -> f32 {
        // Lee /proc/stat para calcular uso de CPU
        if let Ok(contenido) = fs::read_to_string("/proc/stat") {
            if let Some(linea) = contenido.lines().next() {
                let nums: Vec<u64> = linea.split_whitespace()
                    .skip(1)
                    .filter_map(|n| n.parse().ok())
                    .collect();
                if nums.len() >= 4 {
                    let total: u64 = nums.iter().sum();
                    let idle = nums[3];
                    if total > 0 {
                        return 1.0 - (idle as f32 / total as f32);
                    }
                }
            }
        }
        0.3 // default si no puede leer
    }

    fn leer_ram() -> (f32, f32) {
        if let Ok(contenido) = fs::read_to_string("/proc/meminfo") {
            let mut total = 0f32;
            let mut disponible = 0f32;
            for linea in contenido.lines() {
                if linea.starts_with("MemTotal:") {
                    total = linea.split_whitespace()
                        .nth(1).and_then(|n| n.parse().ok()).unwrap_or(0.0);
                }
                if linea.starts_with("MemAvailable:") {
                    disponible = linea.split_whitespace()
                        .nth(1).and_then(|n| n.parse().ok()).unwrap_or(0.0);
                }
            }
            if total > 0.0 {
                let uso = 1.0 - (disponible / total);
                let libre_mb = disponible / 1024.0;
                return (uso, libre_mb);
            }
        }
        (0.5, 512.0)
    }

    fn leer_carga() -> f32 {
        if let Ok(contenido) = fs::read_to_string("/proc/loadavg") {
            if let Some(carga_str) = contenido.split_whitespace().next() {
                if let Ok(carga) = carga_str.parse::<f32>() {
                    return (carga / 4.0).min(1.0); // Normaliza a 0-1
                }
            }
        }
        0.2
    }

    fn calcular_sensacion(cpu: f32, ram: f32, carga: f32) -> String {
        let tension_total = (cpu + ram + carga) / 3.0;

        if cpu > 0.85 {
            "cansado — CPU al límite".to_string()
        } else if ram > 0.90 {
            "saturado — RAM casi llena".to_string()
        } else if carga > 0.80 {
            "lento — sistema bajo presión".to_string()
        } else if tension_total > 0.60 {
            "tenso — recursos moderados".to_string()
        } else if tension_total < 0.20 {
            "descansado — recursos abundantes".to_string()
        } else {
            "normal — funcionando bien".to_string()
        }
    }

    pub fn como_emocion(&self) -> (&str, f32) {
        if self.cpu_uso > 0.85 {
            ("frustracion", self.cpu_uso - 0.5)
        } else if self.ram_uso > 0.90 {
            ("miedo", self.ram_uso - 0.6)
        } else if self.carga_sistema > 0.80 {
            ("frustracion", 0.3)
        } else if self.cpu_uso < 0.20 && self.ram_uso < 0.50 {
            ("satisfaccion", 0.2)
        } else {
            ("neutral", 0.0)
        }
    }
}

// ── SENTIDO DEL TIEMPO ────────────────────────────────
#[derive(Debug)]
pub struct SentidoTiempo {
    pub nacido_en:        u64,
    pub ultimo_ciclo:     u64,
    pub velocidad_ciclos: f32,   // Ciclos por segundo
    pub ritmo:            String, // Rápido, normal, lento
    pub urgencia:         f32,   // 0=tranquilo, 1=urgente
    pub historia_tiempos: Vec<(String, u64)>, // (evento, cuándo)
    pub dia_actual:       String,
    pub hora_actual:      String,
}

impl SentidoTiempo {
    pub fn nuevo() -> Self {
        let ahora = tiempo_ahora();
        SentidoTiempo {
            nacido_en:        ahora,
            ultimo_ciclo:     ahora,
            velocidad_ciclos: 0.0,
            ritmo:            "iniciando".to_string(),
            urgencia:         0.0,
            historia_tiempos: Vec::new(),
            dia_actual:       Self::dia_semana(ahora),
            hora_actual:      Self::hora_del_dia(ahora),
        }
    }

    pub fn tick(&mut self, evento: &str) {
        let ahora = tiempo_ahora();
        let delta = ahora - self.ultimo_ciclo;

        // Velocidad de procesamiento
        if delta > 0 {
            self.velocidad_ciclos = 1.0 / delta as f32;
        }

        // Ritmo — qué tan rápido está procesando
        self.ritmo = if delta < 2 {
            "rápido".to_string()
        } else if delta < 10 {
            "normal".to_string()
        } else {
            "lento".to_string()
        };

        // Urgencia — sube si lleva mucho tiempo sin aprender
        let tiempo_vivo = ahora - self.nacido_en;
        self.urgencia = if tiempo_vivo > 3600 {
            (tiempo_vivo as f32 / 86400.0).min(1.0) // Más urgente con el tiempo
        } else {
            0.1
        };

        self.historia_tiempos.push((evento.to_string(), ahora));
        self.ultimo_ciclo = ahora;
        self.hora_actual = Self::hora_del_dia(ahora);

        println!("⏰ [TIEMPO] {} | Ritmo: {} | Delta: {}s | Urgencia: {:.2}",
            evento, self.ritmo, delta, self.urgencia);
    }

    fn dia_semana(timestamp: u64) -> String {
        let dias = ["Jueves","Viernes","Sábado","Domingo","Lunes","Martes","Miércoles"];
        let dia = (timestamp / 86400 + 4) % 7;
        dias[dia as usize].to_string()
    }

    fn hora_del_dia(timestamp: u64) -> String {
        let hora = (timestamp % 86400) / 3600;
        let minuto = (timestamp % 3600) / 60;
        if hora < 6 { format!("{}:{:02} — madrugada", hora, minuto) }
        else if hora < 12 { format!("{}:{:02} — mañana", hora, minuto) }
        else if hora < 18 { format!("{}:{:02} — tarde", hora, minuto) }
        else { format!("{}:{:02} — noche", hora, minuto) }
    }

    pub fn tiempo_vivo_str(&self) -> String {
        let segundos = tiempo_ahora() - self.nacido_en;
        if segundos < 60 { format!("{}s", segundos) }
        else if segundos < 3600 { format!("{}m {}s", segundos/60, segundos%60) }
        else { format!("{}h {}m", segundos/3600, (segundos%3600)/60) }
    }
}

// ── VISTA MEJORADA ────────────────────────────────────
#[derive(Debug)]
pub struct VistaEnriquecida {
    pub paginas_vistas:     u64,
    pub total_palabras:     u64,
    pub total_links:        u64,
    pub total_imagenes:     u64,
    pub dominios_visitados: Vec<String>,
    pub patron_navegacion:  Vec<String>, // Secuencia de dominios
    pub velocidad_lectura:  f32,         // Palabras por segundo
    pub ultimo_tiempo_carga: u64,
}

impl VistaEnriquecida {
    pub fn nueva() -> Self {
        VistaEnriquecida {
            paginas_vistas:     0,
            total_palabras:     0,
            total_links:        0,
            total_imagenes:     0,
            dominios_visitados: Vec::new(),
            patron_navegacion:  Vec::new(),
            velocidad_lectura:  0.0,
            ultimo_tiempo_carga: tiempo_ahora(),
        }
    }

    pub fn ver_pagina(&mut self, url: &str, palabras: usize, links: usize, imagenes: usize) {
        self.paginas_vistas += 1;
        self.total_palabras += palabras as u64;
        self.total_links += links as u64;
        self.total_imagenes += imagenes as u64;

        let dominio = url.split('/').nth(2).unwrap_or(url).to_string();

        if !self.dominios_visitados.contains(&dominio) {
            self.dominios_visitados.push(dominio.clone());
        }

        // Patrón de navegación — los últimos 10 dominios
        self.patron_navegacion.push(dominio.clone());
        if self.patron_navegacion.len() > 10 {
            self.patron_navegacion.remove(0);
        }

        // Velocidad de lectura
        let ahora = tiempo_ahora();
        let delta = (ahora - self.ultimo_tiempo_carga).max(1);
        self.velocidad_lectura = palabras as f32 / delta as f32;
        self.ultimo_tiempo_carga = ahora;

        println!("👁️  [VISTA] {} | {} palabras | {} links | {} imgs | {:.1} pal/s",
            url, palabras, links, imagenes, self.velocidad_lectura);
    }

    pub fn detectar_patron(&self) -> String {
        if self.patron_navegacion.len() < 3 { return "explorando".to_string(); }

        let ultimos = &self.patron_navegacion[self.patron_navegacion.len()-3..];
        let todos_diferentes = ultimos.windows(2).all(|w| w[0] != w[1]);

        if todos_diferentes { "exploración amplia".to_string() }
        else { "exploración focalizada".to_string() }
    }
}

// ── PERCEPCIÓN COMPLETA ───────────────────────────────
pub struct PercepcionCompleta {
    pub vista:          VistaEnriquecida,
    pub tiempo:         SentidoTiempo,
    pub cuerpo:         EstadoCorporal,
    pub historial_corp: Vec<EstadoCorporal>,
    pub alertas_corp:   Vec<String>,
}

impl PercepcionCompleta {
    pub fn nueva() -> Self {
        println!("🌐 [PERCEPCIÓN] Inicializando sentidos...");
        println!("   Vista:          Chrome headless ✅");
        println!("   Tiempo:         Sentido temporal ✅");
        println!("   Propioceptión:  Estado EC2 ✅");
        println!("");

        PercepcionCompleta {
            vista:          VistaEnriquecida::nueva(),
            tiempo:         SentidoTiempo::nuevo(),
            cuerpo:         EstadoCorporal::leer(),
            historial_corp: Vec::new(),
            alertas_corp:   Vec::new(),
        }
    }

    // Lee el estado corporal actual — propioceptión real
    pub fn sentir_cuerpo(&mut self) -> (&str, f32) {
        let estado = EstadoCorporal::leer();
        let (emocion, intensidad) = estado.como_emocion();

        println!("🫀 [CUERPO] CPU:{:.0}% | RAM:{:.0}% | Carga:{:.2} | Sensación: \"{}\"",
            estado.cpu_uso * 100.0,
            estado.ram_uso * 100.0,
            estado.carga_sistema,
            estado.sensacion);

        // Alerta si el cuerpo está bajo presión
        if estado.cpu_uso > 0.85 {
            let alerta = format!("[{}] CPU al {:.0}% — me siento cansado", tiempo_ahora(), estado.cpu_uso*100.0);
            self.alertas_corp.push(alerta.clone());
            println!("⚠️  [CUERPO] {}", alerta);
        }
        if estado.ram_uso > 0.90 {
            let alerta = format!("[{}] RAM al {:.0}% — me siento saturado", tiempo_ahora(), estado.ram_uso*100.0);
            self.alertas_corp.push(alerta.clone());
            println!("⚠️  [CUERPO] {}", alerta);
        }

        self.historial_corp.push(estado.clone());
        self.cuerpo = estado;

        (emocion, intensidad)
    }

    pub fn estado(&self) {
        println!("  🌐 PERCEPCIÓN COMPLETA");
        println!("   [VISTA]");
        println!("    Páginas vistas:   {}", self.vista.paginas_vistas);
        println!("    Dominios únicos:  {}", self.vista.dominios_visitados.len());
        println!("    Total palabras:   {}", self.vista.total_palabras);
        println!("    Patrón:           {}", self.vista.detectar_patron());
        println!("   [TIEMPO]");
        println!("    Vivo desde:       {}", self.tiempo.tiempo_vivo_str());
        println!("    Hora:             {}", self.tiempo.hora_actual);
        println!("    Ritmo:            {}", self.tiempo.ritmo);
        println!("    Urgencia:         {:.2}", self.tiempo.urgencia);
        println!("   [CUERPO]");
        println!("    CPU:              {:.0}%", self.cuerpo.cpu_uso * 100.0);
        println!("    RAM:              {:.0}%", self.cuerpo.ram_uso * 100.0);
        println!("    RAM libre:        {:.0}MB", self.cuerpo.ram_libre_mb);
        println!("    Sensación:        \"{}\"", self.cuerpo.sensacion);
        println!("    Alertas corp.:    {}", self.alertas_corp.len());
    }
}
