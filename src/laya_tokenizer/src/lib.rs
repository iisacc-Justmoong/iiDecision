use serde_json::Value;
use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use tokenizers::Tokenizer;

struct Package {
    tokenizer: Tokenizer,
    config: Value,
    mask: String,
    special: [u32; 4],
    budgets: [usize; 2],
}

unsafe fn error(out: *mut *mut c_char, message: String) {
    if !out.is_null() {
        *out = CString::new(message.replace('\0', " ")).unwrap().into_raw();
    }
}

fn load(path: &str) -> Result<Package, String> {
    let root = Path::new(path);
    let read = |p: &Path| -> Result<Value, String> {
        let data = std::fs::read(p).map_err(|e| format!("{}: {e}", p.display()))?;
        serde_json::from_slice(&data).map_err(|e| format!("{}: {e}", p.display()))
    };
    let config = read(&root.join("rl_agent_config.json"))?;
    if !config.is_object() { return Err("rl_agent_config.json must be an object".into()); }
    let mut budgets = [512usize, 192usize];
    for (i, name) in ["max_len", "head_max_len"].iter().enumerate() {
        if let Some(v) = config.get(name) {
            let n = v.as_u64().ok_or_else(|| format!("{name} must be a positive integer"))?;
            if n == 0 || n > 65536 { return Err(format!("{name} must be in [1,65536]")); }
            budgets[i] = n as usize;
        }
    }
    let tok_config = read(&root.join("tokenizer/tokenizer_config.json"))?;
    let mut tokenizer = Tokenizer::from_file(root.join("tokenizer/tokenizer.json"))
        .map_err(|e| e.to_string())?;
    tokenizer.with_truncation(None).map_err(|e| e.to_string())?;
    tokenizer.with_padding(None);
    let mut special = [0u32; 4];
    let mut mask = String::new();
    for (i, name) in ["cls_token", "sep_token", "pad_token", "mask_token"].iter().enumerate() {
        let raw = tok_config.get(name).ok_or_else(|| format!("missing {name}"))?;
        let text = raw.as_str().or_else(|| raw.get("content").and_then(Value::as_str))
            .ok_or_else(|| format!("invalid {name}"))?;
        if text.is_empty() { return Err(format!("empty {name}")); }
        special[i] = tokenizer.token_to_id(text).ok_or_else(|| format!("unknown {name}: {text}"))?;
        if i == 3 { mask = text.into(); }
    }
    Ok(Package { tokenizer, config, mask, special, budgets })
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_open(path: *const c_char, err: *mut *mut c_char) -> *mut std::ffi::c_void {
    match catch_unwind(AssertUnwindSafe(|| {
        CStr::from_ptr(path).to_str().map_err(|e| e.to_string()).and_then(load)
    })) {
        Ok(Ok(p)) => Box::into_raw(Box::new(p)).cast(),
        Ok(Err(e)) => { error(err, e); std::ptr::null_mut() },
        Err(_) => { error(err, "tokenizer load panicked".into()); std::ptr::null_mut() },
    }
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_close(handle: *mut std::ffi::c_void) {
    if !handle.is_null() { drop(Box::from_raw(handle.cast::<Package>())); }
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_config(handle: *const std::ffi::c_void, budgets: *mut usize, special: *mut u32) {
    let p = &*handle.cast::<Package>();
    std::ptr::copy_nonoverlapping(p.budgets.as_ptr(), budgets, 2);
    std::ptr::copy_nonoverlapping(p.special.as_ptr(), special, 4);
}

fn temperature(v: Option<&Value>) -> f64 {
    let n = v.and_then(|v| v.as_f64().or_else(|| v.as_str().and_then(|s| s.parse().ok()))).unwrap_or(1.0);
    if n.is_finite() { n.clamp(0.5, 5.0) } else { 1.0 }
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_temperature(handle: *const std::ffi::c_void, qtype: u32, count: usize) -> f64 {
    let p = &*handle.cast::<Package>();
    let kind = ["choice", "score", "noul"][qtype as usize];
    let bucket = if count <= 2 { "2" } else if count <= 5 { "3-5" } else if count <= 10 { "6-10" } else { "11+" };
    let key = format!("{kind}:{bucket}");
    let specific = p.config.get("temperature_by_options").and_then(|v| v.get(&key));
    let default = p.config.get("temperature").and_then(|v| v.get(qtype as usize));
    temperature(specific.or(default))
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_encode(handle: *const std::ffi::c_void, text: *const u8, len: usize,
    ids: *mut *mut u32, count: *mut usize, err: *mut *mut c_char) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<Box<[u32]>, String> {
        let p = &*handle.cast::<Package>();
        let text = std::str::from_utf8(std::slice::from_raw_parts(text, len)).map_err(|e| e.to_string())?;
        let clean = text.replace(&p.mask, " ");
        let encoded = p.tokenizer.encode(clean.as_str(), false).map_err(|e| e.to_string())?;
        Ok(encoded.get_ids().to_vec().into_boxed_slice())
    }));
    match result {
        Ok(Ok(tokens)) => { *count = tokens.len(); *ids = Box::into_raw(tokens) as *mut u32; 0 },
        Ok(Err(e)) => { error(err,e); -1 },
        Err(_) => { error(err,"tokenizer encode panicked".into()); -1 },
    }
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_tokens_free(ids: *mut u32, count: usize) {
    if !ids.is_null() { drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(ids,count))); }
}

#[no_mangle]
pub unsafe extern "C" fn iid_laya_error_free(err: *mut c_char) {
    if !err.is_null() { drop(CString::from_raw(err)); }
}
