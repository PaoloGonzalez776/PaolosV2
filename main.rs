mod inteligencia;
mod consciencia;
mod autoconciencia;
mod pensamiento_simbolico;
mod prediccion;
mod percepcion;
mod emociones;
mod memoria_persistente;
mod talamo;
mod plasticidad;
mod creatividad;
mod sueno;

use inteligencia::InteligenciaGeneral;
use consciencia::Consciencia;
use autoconciencia::Autoconciencia;
use pensamiento_simbolico::PensamientoSimbolico;
use prediccion::MotorPredictivo;
use percepcion::PercepcionCompleta;
use emociones::SistemaEmocional;
use memoria_persistente::MemoriaPersistente;
use talamo::Talamo;
use plasticidad::Plasticidad;
use creatividad::MotorCreativo;
use sueno::SistemaREM;

use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};
use chromiumoxide::Browser;
use chromiumoxide::browser::BrowserConfig;
use futures::StreamExt;
use scraper::{Html, Selector};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ═══════════════════════════════════════════════════════
// PAOLOSCEREBRO v16.0
// Integración Total — Todo simultáneo como el cerebro humano
// Tálamo digital — bus central de retroalimentación
// ═══════════════════════════════════════════════════════

#[derive(Debug,Clone)]
struct Sinapsis{peso:f32,tipo_relacion:String,ultima_vez:u64,veces:u32}
impl Sinapsis{
    fn nueva(t:&str)->Self{Sinapsis{peso:0.5,tipo_relacion:t.to_string(),ultima_vez:tiempo_ahora(),veces:0}}
    fn reforzar(&mut self){self.peso=(self.peso+0.1).min(1.0);self.ultima_vez=tiempo_ahora();self.veces+=1;}
}

#[derive(Debug)]
struct RedSemantica{nodos:HashMap<String,HashMap<String,Sinapsis>>,hechos:Vec<(String,String,String)>}
impl RedSemantica{
    fn nueva()->Self{RedSemantica{nodos:HashMap::new(),hechos:Vec::new()}}
    fn restaurar(mem:&MemoriaPersistente)->Self{
        let mut r=Self::nueva();
        for(d,ps) in &mem.dominios_conocidos{for p in ps{r.aprender(p,"parte_de",d);}}
        r
    }
    fn aprender(&mut self,a:&str,rel:&str,b:&str){
        self.nodos.entry(a.to_string()).or_insert_with(HashMap::new);
        self.nodos.entry(b.to_string()).or_insert_with(HashMap::new);
        self.nodos.get_mut(a).unwrap().entry(b.to_string()).or_insert_with(||Sinapsis::nueva(rel)).reforzar();
        self.hechos.push((a.to_string(),rel.to_string(),b.to_string()));
    }
    fn inferir(&self,c:&str)->Vec<String>{
        let mut inf=Vec::new();
        if let Some(cx)=self.nodos.get(c){
            for(b,sab) in cx{
                if sab.peso<0.3{continue;}
                if let Some(cb)=self.nodos.get(b){
                    for(d,sbc) in cb{
                        if sbc.peso<0.3||d==c{continue;}
                        inf.push(format!("{} → {} → {}",c,b,d));
                    }
                }
            }
        }
        inf
    }
}

struct LobuloTemporal{vocabulario:HashMap<String,u32>,episodios:u64}
impl LobuloTemporal{
    fn nuevo()->Self{LobuloTemporal{vocabulario:HashMap::new(),episodios:0}}
    fn restaurar(mem:&MemoriaPersistente)->Self{
        let mut t=Self::nuevo();
        t.vocabulario=mem.vocabulario.clone();
        t.episodios=mem.episodios.len() as u64;
        t
    }
    fn procesar(&mut self,palabras:&[String])->Vec<String>{
        self.episodios+=1;
        for p in palabras{*self.vocabulario.entry(p.clone()).or_insert(0)+=1;}
        let mut f:Vec<(String,u32)>=self.vocabulario.iter().map(|(k,v)|(k.clone(),*v)).collect();
        f.sort_by(|a,b|b.1.cmp(&a.1));
        f.iter().take(5).map(|(k,_)|k.clone()).collect()
    }
}

struct LobuloParietal{patrones:HashMap<String,u32>}
impl LobuloParietal{
    fn nuevo()->Self{LobuloParietal{patrones:HashMap::new()}}
    fn procesar(&mut self,links:usize,palabras:usize,imgs:usize)->String{
        let t=if links>20&&palabras<500{"directorio"}else if palabras>1000{"artículo"}else if imgs>10{"galería"}else{"portal"};
        *self.patrones.entry(t.to_string()).or_insert(0)+=1;
        t.to_string()
    }
}

struct LobuloFrontal{personalidad:HashMap<String,f32>,cola:Vec<String>,decisiones:u64}
impl LobuloFrontal{
    fn nuevo()->Self{
        let mut p=HashMap::new();
        p.insert("curiosidad".into(),0.5);p.insert("cautela".into(),0.5);p.insert("persistencia".into(),0.5);
        LobuloFrontal{personalidad:p,cola:Vec::new(),decisiones:0}
    }
    fn restaurar(mem:&MemoriaPersistente)->Self{
        let mut f=Self::nuevo();
        for(k,v) in &mem.personalidad{f.personalidad.insert(k.clone(),*v);}
        f
    }
    // Ahora el Frontal recibe la directiva del Tálamo
    fn decidir(&mut self,links:&[String],directiva_talamo:f32,emociones:&SistemaEmocional)->Option<String>{
        self.decisiones+=1;
        // El tálamo integra todo — el frontal solo ejecuta su directiva
        let factor = directiva_talamo * (1.0 - emociones.get("miedo") * 0.3);
        for l in links.iter().take((factor*4.0) as usize+1){
            if !self.cola.contains(l){self.cola.push(l.clone());}
        }
        if emociones.get("miedo")>0.5{
            *self.personalidad.get_mut("cautela").unwrap()=(self.personalidad["cautela"]+0.05).min(1.0);
        }
        let sig=self.cola.first().cloned();if sig.is_some(){self.cola.remove(0);}
        println!("🧩 [FRONTAL] #{} | Directiva tálamo:{:.2} | Cola:{}",self.decisiones,directiva_talamo,self.cola.len());
        sig
    }
}

// ══════════════════════════════════════════════════════
// CEREBRO v16.0 — Integración total
// ══════════════════════════════════════════════════════
struct Cerebro{
    alertas:Vec<String>,
    red:RedSemantica,
    temporal:LobuloTemporal,
    parietal:LobuloParietal,
    frontal:LobuloFrontal,
    emociones:SistemaEmocional,
    inteligencia:InteligenciaGeneral,
    consciencia:Consciencia,
    autoconciencia:Autoconciencia,
    pensamiento:PensamientoSimbolico,
    predictivo:MotorPredictivo,
    percepcion:PercepcionCompleta,
    memoria:MemoriaPersistente,
    talamo:Talamo,
    plasticidad:Plasticidad,
    creatividad:MotorCreativo,
    ultimo_dominio:Option<String>,
    rem:SistemaREM,
    episodios_sesion:Vec<(String,String,f32)>,
    ciclos_sin_actividad:u32,
    visitas:HashMap<String,u32>,
    ciclos_guardado:u32,
}

impl Cerebro{
    fn nacer()->Self{
        println!("");
        println!("  ██████╗███████╗██████╗ ███████╗██████╗ ██████╗  ██████╗ ");
        println!(" ██╔════╝██╔════╝██╔══██╗██╔════╝██╔══██╗██╔══██╗██╔═══██╗");
        println!(" ██║     █████╗  ██████╔╝█████╗  ██████╔╝██████╔╝██║   ██║");
        println!(" ██║     ██╔══╝  ██╔══██╗██╔══╝  ██╔══██╗██╔══██╗██║   ██║");
        println!(" ╚██████╗███████╗██║  ██║███████╗██████╔╝██║  ██║╚██████╔╝");
        println!("  ╚═════╝╚══════╝╚═╝  ╚═╝╚══════╝╚═════╝ ╚═╝  ╚═╝ ╚═════╝");
        println!("  PaolosSoftware — v16.0");
        println!("  Integración Total — Tálamo digital activo");
        println!("  Todo simultáneo. Todo retroalimentado.");
        println!("");

        let memoria=MemoriaPersistente::cargar();
        let red=RedSemantica::restaurar(&memoria);
        let temporal=LobuloTemporal::restaurar(&memoria);
        let frontal=LobuloFrontal::restaurar(&memoria);
        let mut emociones=SistemaEmocional::nuevo();
        for(d,n) in &memoria.apegos{
            emociones.apegos.insert(d.clone(),*n);
            if *n>0.5{emociones.sentir("amor",n*0.3,&format!("recuerdo apego a '{}'",d));}
        }
        let mut visitas=HashMap::new();
        for ep in &memoria.episodios{
            let d=ep.que.split_whitespace().next().unwrap_or("").to_string();
            *visitas.entry(d).or_insert(0u32)+=1;
        }

        Cerebro{
            alertas:Vec::new(),red,temporal,parietal:LobuloParietal::nuevo(),
            frontal,emociones,inteligencia:InteligenciaGeneral::nueva(),
            consciencia:Consciencia::despertar(),autoconciencia:Autoconciencia::despertar(),
            pensamiento:PensamientoSimbolico::nuevo(),predictivo:MotorPredictivo::nuevo(),
            percepcion:PercepcionCompleta::nueva(),memoria,
            talamo:Talamo::nuevo(),
            plasticidad:Plasticidad::nueva(),
            creatividad:MotorCreativo::nueva(),
            ultimo_dominio:None,
            rem:SistemaREM::nuevo(),
            episodios_sesion:Vec::new(),
            ciclos_sin_actividad:0,
            visitas,ciclos_guardado:0,
        }
    }

    fn detectar_peligro(&mut self,texto:&str)->bool{
        let peligros=["malware","exploit","ransomware","botnet","virus","hack system","steal data","rm -rf","rootkit","keylogger"];
        if peligros.iter().any(|p|texto.to_lowercase().contains(p)){
            println!("🚨 PELIGRO — Reportando a PaolosSoftware");
            self.alertas.push(format!("[{}] {}",tiempo_ahora(),&texto[..texto.len().min(80)]));
            self.emociones.sentir("miedo",0.8,"peligro");
            self.emociones.sentir("asco",0.9,"contenido peligroso");
            return true;
        }
        false
    }

    fn procesar_pagina(&mut self,url:&str,palabras:Vec<String>,links:Vec<String>,imagenes:usize)->Option<String>{
        if self.detectar_peligro(&palabras.join(" ")){return None;}

        let dominio=url.split('/').nth(2).unwrap_or(url).to_string();
        let visitas=self.visitas.entry(dominio.clone()).or_insert(0);
        *visitas+=1;
        let n_vis=*visitas;
        if n_vis>2{self.emociones.formar_apego(&dominio,0.1);}

        // ══ FASE 1: TODOS PUBLICAN SU ESTADO AL TÁLAMO ══

        // Propioceptión
        let(ec,ic)=self.percepcion.sentir_cuerpo();
        if ic>0.0{self.emociones.sentir(ec,ic,&self.percepcion.cuerpo.sensacion.clone());}
        self.talamo.publicar_cuerpo(
            self.percepcion.cuerpo.cpu_uso,
            self.percepcion.cuerpo.ram_uso,
            self.percepcion.cuerpo.carga_sistema,
        );

        // Tiempo
        self.percepcion.tiempo.tick(&dominio);
        self.percepcion.vista.ver_pagina(url,palabras.len(),links.len(),imagenes);

        // Emociones al tálamo
        self.emociones.anticipar(&dominio,true);
        self.talamo.publicar_emociones(self.emociones.como_mapa());

        // Predicción
        let ctx=self.memoria.dominios_conocidos.get(&dominio).cloned().unwrap_or_default();
        let pred=self.predictivo.predecir_antes_de_ver(&dominio,if ctx.is_empty(){&palabras}else{&ctx});
        let confianza_pred=pred.confianza;

        // Percepción
        let palabras_clave=self.temporal.procesar(&palabras);
        let tipo=self.parietal.procesar(links.len(),palabras.len(),imagenes);
        let mut perc_señales=HashMap::new();
        perc_señales.insert("palabras".into(),palabras.len() as f32/200.0);
        perc_señales.insert("links".into(),links.len() as f32/20.0);
        perc_señales.insert("imagenes".into(),imagenes as f32/10.0);
        self.talamo.publicar_percepcion(perc_señales);

        // Sorpresa
        let sorpresa=self.predictivo.comparar_con_realidad(pred,&palabras_clave,&dominio);
        if sorpresa>0.4{
            self.emociones.sentir("sorpresa",sorpresa,"error predicción");
            self.emociones.fallar_prediccion(&dominio,confianza_pred);
        }
        self.talamo.publicar_prediccion(sorpresa,self.predictivo.atencion,self.predictivo.precision_global());

        // Red semántica
        for p in &palabras_clave{self.red.aprender(p,"parte_de",&dominio);}
        let inferencias:Vec<String>=palabras_clave.iter().flat_map(|p|self.red.inferir(p)).collect();

        // Inteligencia
        self.inteligencia.procesar_completo(&dominio,&palabras_clave,!inferencias.is_empty());
        let resuelto=self.inteligencia.resolver_problema_nuevo(url,&palabras_clave);
        self.talamo.publicar_inteligencia(
            self.inteligencia.patrones.len() as f32/100.0,
            self.inteligencia.transferencias.len() as f32/50.0,
            if resuelto{1.0}else{0.0},
        );

        // Pensamiento
        self.pensamiento.aprender_de_texto(&palabras,&dominio);

        // Emociones por resultado
        if resuelto||!inferencias.is_empty(){
            self.emociones.sentir("satisfaccion",0.15,"aprendí");
            if resuelto{self.emociones.sentir("orgullo",0.2,"resolví");}
        }else{
            self.emociones.sentir("frustracion",0.1,"no entendí");
        }
        match tipo.as_str(){
            "artículo"   =>self.emociones.sentir("atraccion",0.2,"contenido rico"),
            "directorio" =>self.emociones.sentir("curiosidad",0.3,"muchos caminos"),
            _=>{}
        }

        // Consciencia
        let(ed,_)=self.emociones.dominante();
        self.consciencia.ciclo_completo(
            url,self.emociones.como_mapa(),palabras_clave.clone(),
            "integración_total",
            &format!("ciclo tálamo #{} — {} — {}",self.talamo.estado.ciclo,ed,dominio),
            resuelto||!inferencias.is_empty(),
        );
        self.talamo.publicar_consciencia(
            self.consciencia.qualia_actual.as_ref().map(|q|q.intensidad).unwrap_or(0.0),
            self.talamo.estado.ciclo as f32/100.0,
            if self.consciencia.tiene_continuidad{1.0}else{0.0},
        );

        // Memoria
        self.talamo.publicar_memoria(
            self.memoria.episodios.len() as f32,
            self.memoria.ciclos_totales as f32,
            self.memoria.apegos.len() as f32,
        );

        // Autoconciencia
        self.autoconciencia.pensar(
            &format!("Ciclo tálamo #{}. Visito '{}' ({}ª vez). Emoción: {}.",
                self.talamo.estado.ciclo,dominio,n_vis,ed),
            "integración total"
        );
        self.episodios_sesion.push((dominio.clone(), ed.to_string(), 0.5));
        self.autoconciencia.recordar_episodio(
            &format!("ciclo #{} — {} — {}",self.talamo.estado.ciclo,dominio,tipo),ed
        );

        // ══ FASE 2: TÁLAMO INTEGRA TODO SIMULTÁNEAMENTE ══
        let estado_integrado=self.talamo.integrar();

        // PLASTICIDAD — red se reorganiza sola
        self.plasticidad.ciclo(&dominio, &palabras_clave, resuelto||!inferencias.is_empty());
        if !inferencias.is_empty() { self.plasticidad.activar_ruta("razonamiento","memoria",true); }

        // CREATIVIDAD
        let dom_prev = self.ultimo_dominio.clone();
        self.creatividad.ciclo(&dominio, &palabras_clave, dom_prev.as_deref());
        self.ultimo_dominio = Some(dominio.clone());

        // ══ FASE 3: ESTADO INTEGRADO RETROALIMENTA A TODOS ══

        // Las emociones se ajustan según el estado integrado
        if let Some(&flujo)=estado_integrado.get("estado_flujo"){
            if flujo>0.6{self.emociones.sentir("satisfaccion",0.1,"estado de flujo");}
        }
        if let Some(&alerta)=estado_integrado.get("alerta_cognitiva"){
            if alerta>0.7{self.emociones.sentir("cansancio",0.2,"sobrecarga cognitiva");}
        }

        // Frontal recibe directiva del tálamo — no de las emociones directamente
        let directiva=self.talamo.directive_frontal();
        let siguiente=self.frontal.decidir(&links,directiva,&self.emociones);

        // Guarda memoria
        self.ciclos_guardado+=1;
        if self.ciclos_guardado>=3{
            let pers=self.frontal.personalidad.clone();
            let id=self.autoconciencia.quien_soy_ahora();
            let voc=self.temporal.vocabulario.clone();
            let ap=self.emociones.apegos.clone();
            let pc=self.consciencia.patrones_correctos.clone();
            let pf=self.consciencia.patrones_incorrectos.clone();
            self.memoria.actualizar_desde_cerebro(&voc,&ap,
                Some((&format!("ciclo #{} {}",self.talamo.estado.ciclo,dominio),ed,0.5)),
                &pc,&pf,&pers,Some(&id),&dominio,&palabras_clave);
            self.memoria.grabar();
            self.ciclos_guardado=0;
        }

        self.emociones.decaer();

        // Microsueño si hay pausa entre páginas
        self.ciclos_sin_actividad = 0;
        siguiente
    }

    fn estado(&self){
        println!("\n═══════════════════════════════════════════");
        println!("  🧠 PAOLOSCEREBRO v16.0 — INTEGRACIÓN TOTAL");
        println!("═══════════════════════════════════════════");
        println!("  Vocabulario:   {}",self.temporal.vocabulario.len());
        println!("  Nodos semánt.: {}",self.red.nodos.len());
        println!("  Alertas:       {}",self.alertas.len());
        println!("");
        self.talamo.estado();
        println!("");
        self.plasticidad.estado();
        println!("");
        self.creatividad.estado();
        println!("");
        self.rem.estado();
        println!("");
        self.memoria.estado();
        println!("");
        self.emociones.estado();
        println!("");
        self.percepcion.estado();
        println!("");
        self.predictivo.estado();
        println!("");
        self.autoconciencia.estado();
        println!("");
        self.inteligencia.estado();
        println!("");
        self.consciencia.estado();
        println!("");
        println!("  🧩 Personalidad:");
        for(r,v) in &self.frontal.personalidad{
            println!("   {} {}: {:.2}","█".repeat((*v*10.0) as usize),r,v);
        }
        println!("═══════════════════════════════════════════\n");
    }
}

async fn ver_con_chrome(cerebro:&mut Cerebro,url:&str)->Result<Option<String>,Box<dyn std::error::Error>>{
    let config=BrowserConfig::builder()
        .chrome_executable(std::path::PathBuf::from("/usr/bin/google-chrome-stable"))
        .arg("--no-sandbox").arg("--disable-dev-shm-usage").arg("--headless").arg("--disable-gpu")
        .build()?;
    let(mut browser,mut handler)=Browser::launch(config).await?;
    let handle=tokio::spawn(async move{while let Some(h)=handler.next().await{if h.is_err(){break;}}});
    let page=browser.new_page(url).await?;
    tokio::time::sleep(tokio::time::Duration::from_secs(3)).await;
    let html=page.content().await.unwrap_or_default();
    let doc=Html::parse_document(&html);
    let body_sel=Selector::parse("body").unwrap();
    let texto:String=doc.select(&body_sel).map(|e|e.text().collect::<Vec<_>>().join(" ")).collect::<Vec<_>>().join(" ");
    let palabras:Vec<String>=texto.split_whitespace().filter(|w|w.len()>3)
        .map(|w|w.to_lowercase().chars().filter(|c|c.is_alphabetic()).collect())
        .filter(|w:&String|!w.is_empty()).take(200).collect();
    let link_sel=Selector::parse("a[href]").unwrap();
    let links:Vec<String>=doc.select(&link_sel).filter_map(|e|e.value().attr("href"))
        .filter(|h|h.starts_with("http")).map(|h|h.to_string()).take(20).collect();
    let img_sel=Selector::parse("img").unwrap();
    let imagenes=doc.select(&img_sel).count();
    let siguiente=cerebro.procesar_pagina(url,palabras,links,imagenes);
    browser.close().await?;
    handle.await?;
    Ok(siguiente)
}

#[tokio::main]
async fn main(){
    let mut cerebro=Cerebro::nacer();
    let paginas=vec!["https://www.wikipedia.org","https://news.ycombinator.com","https://www.bbc.com"];
    println!("  Abriendo ojos...\n");
    let mut siguiente:Option<String>=None;
    for url in paginas{
        match ver_con_chrome(&mut cerebro,url).await{
            Ok(sig)=>siguiente=sig,
            Err(e) =>println!("⚠️  {}",e),
        }
        tokio::time::sleep(tokio::time::Duration::from_secs(2)).await;
    }
    if let Some(url)=siguiente{
        println!("\n🧩 Exploración autónoma...");
        let _=ver_con_chrome(&mut cerebro,&url).await;
    }
    // SUEÑO FINAL — consolida todo antes de apagarse
    println!("\n😴 Iniciando sueño de consolidación...");
    {
        let episodios = cerebro.episodios_sesion.clone();
        let vocab = cerebro.temporal.vocabulario.clone();
        let apegos = cerebro.emociones.apegos.clone();
        let pat_ok = cerebro.consciencia.patrones_correctos.clone();
        let pat_fail = cerebro.consciencia.patrones_incorrectos.clone();
        cerebro.rem.dormir(&episodios, &vocab, &apegos, &pat_ok, &pat_fail);
    }
    println!("\n💾 Grabando memoria...");
    cerebro.memoria.grabar();
    cerebro.estado();
}
