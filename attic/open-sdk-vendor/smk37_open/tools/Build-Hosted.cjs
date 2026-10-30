// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Static-site packaging. Keep the standalone download intact.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [input,output]=process.argv.slice(2);assert(input&&output,'Pass the standalone HTML and a new staging directory.');
assert(!fs.existsSync(output),'Use a new staging directory.');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),assets=[];
function asset(data,extension){
  const bytes=Buffer.from(data),hash=sha(bytes),name='assets/smk37/'+hash+'.'+extension;
  const target=path.join(output,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);
  if(!assets.some(x=>x.name===name))assets.push({name,bytes:bytes.length,sha256:hash});
  return './'+name;
}
const standalone=fs.readFileSync(input,'utf8'),download='SMK37-Open-Source.html';
let html=standalone;
const sourceLink=html.match(/<a href="([^"]+)"[^>]*>源码与 SDK<\/a>/);
assert(sourceLink,'Missing source archive link.');
assert.equal(new URL(sourceLink[1]).protocol,'https:','A source archive HTTPS URL is required.');
const packaged=JSON.parse(html.match(/const FM1_PACKAGE = (.*);/)[1]);
const simulator=JSON.parse(html.match(/const FM1_SIMULATOR = (.*);/)[1]);
function external(meta,extension){
  if(!meta)return null;
  const bytes=Buffer.from(meta.data,'base64');assert.equal(sha(bytes),meta.sha256);
  const {data,...description}=meta;
  if(meta.stock)description.stock=external(meta.stock,'bin');
  description.bytes=bytes.length;description.url=asset(bytes,extension);
  return description;
}
html=html.replace(/const FM1_PACKAGE = .*;/,'const FM1_PACKAGE = '+JSON.stringify(external(packaged,'bin'))+';');
html=html.replace(/const FM1_SIMULATOR = .*;/,'const FM1_SIMULATOR = '+JSON.stringify(external(simulator,'wasm'))+';');
const scripts=[];
html=html.replace(/<script>\s*([\s\S]*?)<\/script>/g,(_,code)=>{
  const url=asset(code,'js'),integrity=crypto.createHash('sha256').update(code).digest('base64');
  scripts.push({url,integrity:'sha256-'+integrity});return '';
});
assert.equal(scripts.length,3);assert(!html.includes('const FM1_PACKAGE'));
// Double-clicking either site entry opens the bundled offline edition. Never
// request external file:// scripts: SRI and fetch require an HTTP(S) origin.
// Relative asset URLs also support a product subdirectory on the same host.
html=html.replace('</main>','</main>\n<script data-smk37-entry>\n'+
  'if(location.protocol==="file:"){location.replace(new URL("./'+download+'"+location.search+location.hash,location.href));}\n'+
  'else for(const item of '+JSON.stringify(scripts)+'){const script=document.createElement("script");script.src=item.url;script.integrity=item.integrity;script.crossOrigin="anonymous";script.async=false;document.body.append(script);}\n'+
  '</script>');
assert(html.includes('data-smk37-entry'),'Missing site entry loader.');
for(const name of ['index.html','smk37-workbench.html']){fs.writeFileSync(path.join(output,name),html);assets.push({name,bytes:Buffer.byteLength(html),sha256:sha(html)});}
fs.writeFileSync(path.join(output,download),standalone);
assets.push({name:download,bytes:Buffer.byteLength(standalone),sha256:sha(standalone)});
const initial=assets.filter(x=>!x.name.endsWith('.bin')&&x.name!=='smk37-workbench.html'&&x.name!==download);
const report={build:packaged?.build_id,release:simulator.release_version,entryBytes:Buffer.byteLength(html),initialBytes:initial.reduce((n,x)=>n+x.bytes,0),standaloneBytes:fs.statSync(input).size,assets};
fs.writeFileSync(path.join(output,'assets.json'),JSON.stringify(report,null,2)+'\n');
console.log({entryBytes:report.entryBytes,initialBytes:report.initialBytes,standaloneBytes:report.standaloneBytes,assets:assets.length});
