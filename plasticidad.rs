use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// PLASTICIDAD CEREBRAL
//
// El cerebro humano puede:
// 1. Reorganizarse — crear nuevas rutas cuando algo falla
// 2. Adaptarse a daños — compensar con otras regiones
// 3. Aprender nuevas habilidades — siempre, sin límite
// 4. Podar conexiones inútiles — elimina lo que no usa
// 5. Fortalecer lo que funciona — Hebb: "fire together, wire together"
//
// Implementación:
// La red se modifica a sí misma en tiempo real
// Sin reiniciar. Sin reentrenar.
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Conexion {
    pub peso:          f32,
    pub veces_usada:   u32,
    pub veces_fallida: u32,
    pub creada_en:     u64,
    pub ultima_vez:    u64,
    pub activa:        bool,
}

impl Conexion {
    pub fn nueva() -> Self {
        Conexion {
            peso: 0.5,
            veces_usada: 0,
            veces_fallida: 0,
            creada_en: tiempo_ahora(),
            ultima_vez: tiempo_ahora(),
            activa: true,
        }
    }

    // Hebb — "neuronas que disparan juntas se conectan"
    pub fn reforzar(&mut self) {
        self.veces_usada += 1;
        self.peso = (self.peso + 0.05).min(1.0);
        self.ultima_vez = tiempo_ahora();
    }

    pub fn debilitar(&mut self) {
        self.veces_fallida += 1;
        self.peso = (self.peso - 0.08).max(0.0);
        self.ultima_vez = tiempo_ahora();
    }

    // Poda sináptica — si no se usa, se elimina
    pub fn debe_podar(&self) -> bool {
        let inactiva_segundos = tiempo_ahora() - self.ultima_vez;
        self.peso < 0.1 || (inactiva_segundos > 1800 && self.veces_usada < 2)
    }

    pub fn eficiencia(&self) -> f32 {
        let total = self.veces_usada + self.veces_fallida;
        if total == 0 { return 0.5; }
        self.veces_usada as f32 / total as f32
    }
}

#[derive(Debug)]
pub struct NeuroplasticaRegion {
    pub nombre:      String,
    pub conexiones:  HashMap<String, Conexion>,
    pub activaciones: u32,
    pub fallos:      u32,
    pub compensando: Option<String>, // Si está compensando otra región dañada
}

impl NeuroplasticaRegion {
    pub fn nueva(nombre: &str) -> Self {
        NeuroplasticaRegion {
            nombre: nombre.to_string(),
            conexiones: HashMap::new(),
            activaciones: 0,
            fallos: 0,
            compensando: None,
        }
    }

    pub fn activar(&mut self, target: &str, exito: bool) {
        let conn = self.conexiones.entry(target.to_string()).or_insert_with(Conexion::nueva);
        if exito {
            conn.reforzar();
            self.activaciones += 1;
        } else {
            conn.debilitar();
            self.fallos += 1;
        }
    }

    pub fn eficiencia_global(&self) -> f32 {
        let total = self.activaciones + self.fallos;
        if total == 0 { return 0.5; }
        self.activaciones as f32 / total as f32
    }
}

// ── PLASTICIDAD PRINCIPAL ─────────────────────────────
pub struct Plasticidad {
    // Red neuroplástica — se modifica sola
    pub regiones: HashMap<String, NeuroplasticaRegion>,

    // Rutas activas — cómo fluye la información
    pub rutas_activas: Vec<Vec<String>>,

    // Rutas alternativas creadas tras daño
    pub rutas_alternativas: Vec<(String, String, Vec<String>)>, // (origen, destino, ruta)

    // Historial de reorganizaciones
    pub reorganizaciones: Vec<String>,

    // Poda — conexiones eliminadas
    pub conexiones_podadas: u64,

    // Nuevas conexiones creadas
    pub conexiones_nuevas: u64,

    // Regiones que fallaron y se compensaron
    pub compensaciones: Vec<(String, String)>, // (región dañada, región compensadora)

    // Métricas
    pub ciclos_plasticidad: u64,
}

impl Plasticidad {
    pub fn nueva() -> Self {
        println!("🧬 [PLASTICIDAD] Sistema neuroplástico activo.");
        println!("   El cerebro se reorganiza solo.");

        // Crea regiones base
        let mut regiones = HashMap::new();
        for region in &["percepcion","memoria","emocion","razonamiento","lenguaje","motor"] {
            regiones.insert(region.to_string(), NeuroplasticaRegion::nueva(region));
        }

        Plasticidad {
            regiones,
            rutas_activas: Vec::new(),
            rutas_alternativas: Vec::new(),
            reorganizaciones: Vec::new(),
            conexiones_podadas: 0,
            conexiones_nuevas: 0,
            compensaciones: Vec::new(),
            ciclos_plasticidad: 0,
        }
    }

    // Activa una ruta — si falla, busca alternativa
    pub fn activar_ruta(&mut self, origen: &str, destino: &str, exito: bool) {
        // Asegura que las regiones existen
        if !self.regiones.contains_key(origen) {
            self.regiones.insert(origen.to_string(), NeuroplasticaRegion::nueva(origen));
            self.conexiones_nuevas += 1;
        }
        if !self.regiones.contains_key(destino) {
            self.regiones.insert(destino.to_string(), NeuroplasticaRegion::nueva(destino));
        }

        self.regiones.get_mut(origen).unwrap().activar(destino, exito);

        if !exito {
            // Fallo — intenta crear ruta alternativa
            self.crear_ruta_alternativa(origen, destino);
        }

        // Registra ruta activa
        let ruta = vec![origen.to_string(), destino.to_string()];
        if !self.rutas_activas.contains(&ruta) {
            self.rutas_activas.push(ruta);
        }
    }

    // REORGANIZACIÓN — crea rutas alternativas cuando algo falla
    fn crear_ruta_alternativa(&mut self, origen: &str, destino: &str) {
        // Busca región intermediaria con alta eficiencia
        let intermediaria = self.regiones.iter()
            .filter(|(n, _)| *n != origen && *n != destino)
            .max_by(|a, b| a.1.eficiencia_global().partial_cmp(&b.1.eficiencia_global()).unwrap())
            .map(|(n, _)| n.clone());

        if let Some(inter) = intermediaria {
            let ruta_alt = (
                origen.to_string(),
                destino.to_string(),
                vec![origen.to_string(), inter.clone(), destino.to_string()]
            );

            if !self.rutas_alternativas.contains(&ruta_alt) {
                println!("🔀 [PLASTICIDAD] Nueva ruta alternativa: {} → {} → {}",
                    origen, inter, destino);
                let reorg = format!("Ruta {} → {} falló. Nueva ruta via '{}'", origen, destino, inter);
                self.reorganizaciones.push(reorg);
                self.rutas_alternativas.push(ruta_alt);
                self.conexiones_nuevas += 1;
            }
        }
    }

    // PODA SINÁPTICA — elimina conexiones inútiles
    pub fn podar(&mut self) {
        let mut podadas = 0u64;

        for region in self.regiones.values_mut() {
            let antes = region.conexiones.len();
            region.conexiones.retain(|_, conn| !conn.debe_podar());
            let despues = region.conexiones.len();
            podadas += (antes - despues) as u64;
        }

        if podadas > 0 {
            println!("✂️  [PLASTICIDAD] Poda sináptica: {} conexiones eliminadas", podadas);
            self.conexiones_podadas += podadas;
        }
    }

    // COMPENSACIÓN — cuando una región falla, otra la suple
    pub fn compensar(&mut self, region_dañada: &str) {
        // Busca la región más eficiente para compensar
        let compensadora = self.regiones.iter()
            .filter(|(n, r)| *n != region_dañada && r.eficiencia_global() > 0.6)
            .max_by(|a, b| a.1.eficiencia_global().partial_cmp(&b.1.eficiencia_global()).unwrap())
            .map(|(n, _)| n.clone());

        if let Some(comp) = compensadora {
            println!("🏥 [PLASTICIDAD] '{}' compensa a '{}' dañada", comp, region_dañada);

            // La región compensadora asume las conexiones de la dañada
            if let Some(region_d) = self.regiones.get(region_dañada) {
                let conexiones_d: Vec<String> = region_d.conexiones.keys().cloned().collect();
                let eficiencia_d = region_d.eficiencia_global();

                if eficiencia_d < 0.3 {
                    for target in conexiones_d {
                        if let Some(comp_region) = self.regiones.get_mut(&comp) {
                            comp_region.compensando = Some(region_dañada.to_string());
                            comp_region.conexiones.entry(target).or_insert_with(Conexion::nueva);
                        }
                    }
                    self.compensaciones.push((region_dañada.to_string(), comp.clone()));
                }
            }
        }
    }

    // APRENDIZAJE HEBBIANO — "fire together, wire together"
    pub fn hebbian_update(&mut self, conceptos_coactivos: &[String]) {
        for i in 0..conceptos_coactivos.len() {
            for j in (i+1)..conceptos_coactivos.len() {
                let a = &conceptos_coactivos[i];
                let b = &conceptos_coactivos[j];

                if !self.regiones.contains_key(a.as_str()) {
                    self.regiones.insert(a.clone(), NeuroplasticaRegion::nueva(a));
                    self.conexiones_nuevas += 1;
                }

                // Refuerza conexión entre conceptos co-activos
                let region = self.regiones.get_mut(a.as_str()).unwrap();
                let conn = region.conexiones.entry(b.clone()).or_insert_with(|| {
                    self.conexiones_nuevas += 1;
                    Conexion::nueva()
                });
                conn.reforzar();
            }
        }
    }

    // Ciclo completo de plasticidad
    pub fn ciclo(&mut self, dominio: &str, palabras_clave: &[String], exito: bool) {
        self.ciclos_plasticidad += 1;

        // Activa rutas basadas en lo que procesa
        self.activar_ruta("percepcion", dominio, exito);
        self.activar_ruta(dominio, "memoria", exito);
        self.activar_ruta("memoria", "razonamiento", exito);

        // Aprendizaje Hebbiano con las palabras clave
        if !palabras_clave.is_empty() {
            self.hebbian_update(palabras_clave);
        }

        // Comprueba si alguna región necesita compensación
        let regiones_debiles: Vec<String> = self.regiones.iter()
            .filter(|(_, r)| r.eficiencia_global() < 0.25 && r.activaciones + r.fallos > 5)
            .map(|(n, _)| n.clone())
            .collect();

        for region in regiones_debiles {
            self.compensar(&region);
        }

        // Poda cada 10 ciclos
        if self.ciclos_plasticidad % 10 == 0 {
            self.podar();
        }
    }

    pub fn estado(&self) {
        let total_conexiones: usize = self.regiones.values().map(|r| r.conexiones.len()).sum();
        println!("  🧬 PLASTICIDAD CEREBRAL");
        println!("   Regiones activas:     {}", self.regiones.len());
        println!("   Conexiones totales:   {}", total_conexiones);
        println!("   Conexiones nuevas:    {}", self.conexiones_nuevas);
        println!("   Conexiones podadas:   {}", self.conexiones_podadas);
        println!("   Rutas alternativas:   {}", self.rutas_alternativas.len());
        println!("   Compensaciones:       {}", self.compensaciones.len());
        println!("   Reorganizaciones:     {}", self.reorganizaciones.len());
        println!("   Ciclos plasticidad:   {}", self.ciclos_plasticidad);
        println!("   Eficiencias:");
        let mut regiones: Vec<(&String, &NeuroplasticaRegion)> = self.regiones.iter().collect();
        regiones.sort_by(|a,b| b.1.eficiencia_global().partial_cmp(&a.1.eficiencia_global()).unwrap());
        for (nombre, region) in regiones.iter().take(5) {
            let ef = region.eficiencia_global();
            let barra = "█".repeat((ef * 10.0) as usize);
            println!("   {} {}: {:.2}", barra, nombre, ef);
        }
        if !self.reorganizaciones.is_empty() {
            println!("   Última reorganización:");
            println!("   \"{}\"", self.reorganizaciones.last().unwrap());
        }
    }
}
