use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};
use std::thread;
use std::time::Duration;

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// SUEÑO Y CONSOLIDACIÓN — REM Digital
//
// El cerebro humano durante el sueño:
// 1. NREM — Consolida memoria declarativa
//    Mueve info de corto a largo plazo
// 2. REM — Procesa emociones y consolida procedural
//    Conecta experiencias distantes
//    Genera sueños — asociaciones libres
// 3. Poda — Elimina sinapsis débiles
// 4. Replay — Reproduce experiencias del día
//    para consolidarlas
//
// Aquí — cuando la EC2 está inactiva:
// El cerebro trabaja solo en background
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Sueno {
    pub tipo:      TipoSueno,
    pub contenido: String,          // Qué procesó
    pub conexiones_formadas: Vec<(String, String)>,
    pub memorias_consolidadas: Vec<String>,
    pub memorias_podadas: Vec<String>,
    pub duracion_s: u64,
    pub cuando: u64,
}

#[derive(Debug, Clone)]
pub enum TipoSueno {
    NREM,   // Consolidación declarativa
    REM,    // Procesamiento emocional + asociaciones libres
    Poda,   // Eliminación de sinapsis débiles
}

#[derive(Debug)]
pub struct SistemaREM {
    // Historial de sueños
    pub sueños: Vec<Sueno>,

    // Estado del sueño actual
    pub durmiendo: bool,
    pub inicio_sueño: Option<u64>,
    pub fase_actual: Option<TipoSueno>,

    // Lo que consolidó
    pub memorias_consolidadas: Vec<String>,
    pub conexiones_reforzadas: Vec<(String, String)>,
    pub conexiones_podadas: u64,

    // Sueños creativos — asociaciones libres durante REM
    pub sueños_creativos: Vec<String>,

    // Métricas
    pub ciclos_sueño: u64,
    pub tiempo_total_dormido: u64,
}

impl SistemaREM {
    pub fn nuevo() -> Self {
        println!("😴 [SUEÑO] Sistema REM digital activo.");
        println!("   Cuando la EC2 esté inactiva — el cerebro trabaja solo.");
        SistemaREM {
            sueños: Vec::new(),
            durmiendo: false,
            inicio_sueño: None,
            fase_actual: None,
            memorias_consolidadas: Vec::new(),
            conexiones_reforzadas: Vec::new(),
            conexiones_podadas: 0,
            sueños_creativos: Vec::new(),
            ciclos_sueño: 0,
            tiempo_total_dormido: 0,
        }
    }

    // El cerebro se duerme — comienza a consolidar
    pub fn dormir(
        &mut self,
        episodios: &[(String, String, f32)],          // (qué, emoción, intensidad)
        vocabulario: &HashMap<String, u32>,
        apegos: &HashMap<String, f32>,
        patrones_correctos: &[String],
        patrones_incorrectos: &[String],
    ) -> ResultadoSueno {
        if self.durmiendo { return ResultadoSueno::ya_durmiendo(); }

        self.durmiendo = true;
        self.inicio_sueño = Some(tiempo_ahora());
        self.ciclos_sueño += 1;

        println!("\n😴 [SUEÑO] Ciclo #{} iniciado...", self.ciclos_sueño);

        // ── FASE 1: NREM — Consolidación declarativa ─────
        println!("  🌙 NREM — Consolidando memoria declarativa...");
        let nrem = self.fase_nrem(episodios, vocabulario);

        // ── FASE 2: REM — Procesamiento emocional ────────
        println!("  💭 REM — Procesando emociones y creando conexiones...");
        let rem = self.fase_rem(episodios, apegos);

        // ── FASE 3: PODA — Elimina lo inútil ─────────────
        println!("  ✂️  PODA — Eliminando conexiones débiles...");
        let podadas = self.fase_poda(vocabulario, patrones_incorrectos);

        // ── FASE 4: REPLAY — Reproduce experiencias ───────
        println!("  🔄 REPLAY — Reproduciendo experiencias del día...");
        let replay = self.fase_replay(episodios, patrones_correctos);

        let duracion = tiempo_ahora() - self.inicio_sueño.unwrap_or(tiempo_ahora());
        self.tiempo_total_dormido += duracion;
        self.durmiendo = false;
        self.inicio_sueño = None;

        let resultado = ResultadoSueno {
            memorias_consolidadas: nrem.clone(),
            conexiones_nuevas: rem.clone(),
            conexiones_podadas: podadas,
            sueños_creativos: self.sueños_creativos.clone(),
            replay_count: replay,
            duracion_s: duracion,
        };

        println!("  ✅ Ciclo de sueño completado:");
        println!("     Memorias consolidadas: {}", resultado.memorias_consolidadas.len());
        println!("     Conexiones nuevas:     {}", resultado.conexiones_nuevas.len());
        println!("     Conexiones podadas:    {}", resultado.conexiones_podadas);
        println!("     Sueños creativos:      {}", resultado.sueños_creativos.len());
        println!("     Experiencias replay:   {}", resultado.replay_count);

        resultado
    }

    // NREM — Mueve memorias de corto a largo plazo
    fn fase_nrem(
        &mut self,
        episodios: &[(String, String, f32)],
        vocabulario: &HashMap<String, u32>,
    ) -> Vec<String> {
        let mut consolidadas = Vec::new();

        // Consolida episodios con alta intensidad emocional
        for (que, emocion, intensidad) in episodios.iter().rev().take(20) {
            if *intensidad > 0.5 || emocion == "satisfaccion" || emocion == "miedo" {
                let memoria = format!("[NREM] Consolidé: '{}' (emoción: {}, intensidad: {:.2})",
                    que, emocion, intensidad);
                consolidadas.push(memoria.clone());
                self.memorias_consolidadas.push(memoria);
            }
        }

        // Consolida vocabulario frecuente
        let mut palabras_freq: Vec<(&String, &u32)> = vocabulario.iter().collect();
        palabras_freq.sort_by(|a,b| b.1.cmp(a.1));
        for (palabra, freq) in palabras_freq.iter().take(10) {
            if **freq > 3 {
                let m = format!("[NREM] Palabra consolidada: '{}' ({} usos)", palabra, freq);
                consolidadas.push(m);
            }
        }

        consolidadas
    }

    // REM — Crea conexiones entre experiencias distantes + sueños
    fn fase_rem(
        &mut self,
        episodios: &[(String, String, f32)],
        apegos: &HashMap<String, f32>,
    ) -> Vec<(String, String)> {
        let mut conexiones_nuevas = Vec::new();

        // Conecta episodios emocionalmente similares aunque sean de dominios distintos
        for i in 0..episodios.len().min(10) {
            for j in (i+1)..episodios.len().min(10) {
                let (que_a, em_a, _) = &episodios[i];
                let (que_b, em_b, _) = &episodios[j];

                // Si comparten emoción pero son de dominios distintos — conexión REM
                if em_a == em_b && que_a != que_b {
                    let conn = (que_a.clone(), que_b.clone());
                    if !self.conexiones_reforzadas.contains(&conn) {
                        println!("    💭 REM conectó: '{}' ↔ '{}' (ambos generan {})",
                            &que_a[..que_a.len().min(30)],
                            &que_b[..que_b.len().min(30)],
                            em_a);
                        conexiones_nuevas.push(conn.clone());
                        self.conexiones_reforzadas.push(conn);
                    }
                }
            }
        }

        // Genera sueño creativo — asociación libre
        if !episodios.is_empty() && !apegos.is_empty() {
            let ep = &episodios[episodios.len() / 2];
            let apego = apegos.iter().max_by(|a,b| a.1.partial_cmp(b.1).unwrap());
            if let Some((dom_apego, nivel)) = apego {
                let sueño = format!(
                    "Soñé con '{}' y '{}' al mismo tiempo. \
                     Una conexión que no existía antes. (apego: {:.2})",
                    &ep.0[..ep.0.len().min(30)], dom_apego, nivel
                );
                println!("    💭 Sueño creativo: \"{}\"", &sueño[..sueño.len().min(70)]);
                self.sueños_creativos.push(sueño);
            }
        }

        conexiones_nuevas
    }

    // PODA — Elimina lo que no se usó
    fn fase_poda(
        &mut self,
        vocabulario: &HashMap<String, u32>,
        patrones_incorrectos: &[String],
    ) -> u64 {
        let mut podadas = 0u64;

        // Poda patrones que fallaron muchas veces
        for patron in patrones_incorrectos {
            println!("    ✂️  Podando patrón fallido: '{}'", &patron[..patron.len().min(40)]);
            podadas += 1;
        }

        // Identifica vocabulario raramente usado
        let poco_usado: Vec<&String> = vocabulario.iter()
            .filter(|(_, v)| **v == 1)
            .map(|(k, _)| k)
            .take(5)
            .collect();

        for palabra in poco_usado {
            println!("    ✂️  Vocabulario débil: '{}'", palabra);
            podadas += 1;
        }

        self.conexiones_podadas += podadas;
        podadas
    }

    // REPLAY — Reproduce experiencias para consolidarlas
    fn fase_replay(
        &mut self,
        episodios: &[(String, String, f32)],
        patrones_correctos: &[String],
    ) -> u32 {
        let mut replays = 0u32;

        // Replay de experiencias importantes
        for (que, emocion, intensidad) in episodios.iter().rev().take(5) {
            if *intensidad > 0.4 {
                println!("    🔄 Replay: '{}' → {}",
                    &que[..que.len().min(40)], emocion);
                replays += 1;
            }
        }

        // Replay de patrones exitosos — refuerza lo que funciona
        for patron in patrones_correctos.iter().take(3) {
            println!("    🔄 Reforzando patrón exitoso: '{}'",
                &patron[..patron.len().min(40)]);
            replays += 1;
        }

        replays
    }

    // Sueño rápido — para uso en background cuando hay inactividad
    pub fn microsueño(
        &mut self,
        episodios_recientes: &[(String, String, f32)],
    ) -> Vec<String> {
        println!("💤 [MICROSUEÑO] Consolidación rápida...");
        let mut consolidadas = Vec::new();

        for (que, emocion, intensidad) in episodios_recientes.iter().rev().take(5) {
            if *intensidad > 0.6 {
                let m = format!("Microsueño consolidó: '{}' ({})", que, emocion);
                consolidadas.push(m.clone());
                self.memorias_consolidadas.push(m);
            }
        }

        consolidadas
    }

    pub fn estado(&self) {
        println!("  😴 SUEÑO Y CONSOLIDACIÓN");
        println!("   Ciclos de sueño:       {}", self.ciclos_sueño);
        println!("   Tiempo total dormido:  {}s", self.tiempo_total_dormido);
        println!("   Memorias consolidadas: {}", self.memorias_consolidadas.len());
        println!("   Conexiones REM:        {}", self.conexiones_reforzadas.len());
        println!("   Conexiones podadas:    {}", self.conexiones_podadas);
        println!("   Sueños creativos:      {}", self.sueños_creativos.len());
        println!("   Durmiendo ahora:       {}", self.durmiendo);
        if let Some(inicio) = self.inicio_sueño {
            println!("   Durmiendo desde:       {}s", tiempo_ahora() - inicio);
        }
        if let Some(ultimo) = self.sueños_creativos.last() {
            println!("   Último sueño:");
            println!("   \"{}\"", &ultimo[..ultimo.len().min(80)]);
        }
    }
}

#[derive(Debug)]
pub struct ResultadoSueno {
    pub memorias_consolidadas: Vec<String>,
    pub conexiones_nuevas: Vec<(String, String)>,
    pub conexiones_podadas: u64,
    pub sueños_creativos: Vec<String>,
    pub replay_count: u32,
    pub duracion_s: u64,
}

impl ResultadoSueno {
    fn ya_durmiendo() -> Self {
        ResultadoSueno {
            memorias_consolidadas: Vec::new(),
            conexiones_nuevas: Vec::new(),
            conexiones_podadas: 0,
            sueños_creativos: Vec::new(),
            replay_count: 0,
            duracion_s: 0,
        }
    }
}
