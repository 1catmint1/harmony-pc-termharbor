const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const ts=require(process.env.TT_TYPESCRIPT_PATH || 'typescript');
const f=require('path').join(__dirname,'../ports/ohos/app/entry/src/main/ets/pages/Index.ets');
const src=fs.readFileSync(f,'utf8');
const names=['startLocalExperience','startLocalFileExperience','prepareOwnConnection','stopLocalExperience'];
const methods=names.map(n=>{const m=src.match(new RegExp('^  private '+n+'\\([^]*?(?=^  (?:private |@State |aboutTo))','m'));assert(m,n);return m[0]}).join('\n');
let fail=false,issue='',stopped=0;
const demo={host:'127.0.0.1',tcpPort:12345,sshPort:12346,username:'local-demo',password:'local-demo',remotePath:'/hello.txt'};
const box={TerminalNative:{startLocalExperience(){if(fail)throw Error('Unable to start local listener');return demo},localExperienceStatus(){return {error:issue}},stopLocalExperience(){++stopped}},SandboxTransferStore:{localExperienceUpload(){return '/private/app/local-experience-upload.txt'}}};
vm.createContext(box);vm.runInContext(ts.transpileModule('class Entry {\n'+methods+'\n}\nglobalThis.Entry=Entry;',{compilerOptions:{target:ts.ScriptTarget.ES2020}}).outputText,box);
function setup(){fail=false;issue='';let x=new box.Entry();x.created=0;x.saved=0;x.connected=0;x.toolsVisible=true;x.activeToolsPanel='setup';x.createTab=()=>++x.created;x.saveActiveTab=()=>++x.saved;x.appContext=()=>({});x.connect=()=>{++x.connected;x.status=x.protocol==='ssh'?'SSH connected 127.0.0.1:12346':'TCP connected'};return x}
let cases=[];function test(name,fn){fn();cases.push({name,pass:true})}
test('TCP demo connects loopback with actual runtime port and no proxy',()=>{let x=setup();x.startLocalExperience('tcp');assert.equal(x.host,'127.0.0.1');assert.equal(x.port,'12345');assert.equal(x.proxyEnabled,false);assert.equal(x.connected,1);assert.equal(x.created,1)});
test('SSH demo fills the published local account and clears prior key credentials',()=>{let x=setup();x.privateKeyPath='old';x.publicKeyPath='old';x.passphrase='old';x.startLocalExperience('ssh');assert.equal(x.port,'12346');assert.equal(x.username,'local-demo');assert.equal(x.password,'local-demo');assert.equal(x.privateKeyPath,'');assert.equal(x.publicKeyPath,'');assert.equal(x.passphrase,'')});
test('Demo file paths are ready for private-directory transfer',()=>{let x=setup();x.startLocalExperience('ssh');assert.equal(x.transferRemotePath,'/hello.txt');assert.equal(x.transferLocalPath,'/private/app/local-experience-upload.txt');assert.equal(x.forwardRemotePort,'12345');assert.equal(x.forwardLocalPort,'0')});
test('Terminal demo closes stale tool panels',()=>{let x=setup();x.startLocalExperience('ssh');assert.equal(x.toolsVisible,false);assert.equal(x.activeToolsPanel,'')});
test('File experience connects then opens the populated File tools',()=>{let x=setup();x.startLocalFileExperience();assert.equal(x.protocol,'ssh');assert.equal(x.connected,1);assert.equal(x.toolsVisible,true);assert.equal(x.activeToolsPanel,'file');assert(x.localExperienceText.includes('账号与路径已填好'))});
test('Failed SSH authentication does not present transfer tools as ready',()=>{let x=setup();x.connect=()=>x.status='Authentication failed';x.startLocalFileExperience();assert.equal(x.toolsVisible,false);assert.equal(x.activeToolsPanel,'');assert(!x.localExperienceText.includes('已连接本机 SSH'))});
test('Listener startup failure reports an error without attempting a connection',()=>{let x=setup();fail=true;x.startLocalExperience('ssh');assert.equal(x.connected,0);assert.equal(x.created,0);assert(x.status.includes('Unable to start'))});
test('Runtime issue is visible after a connection attempt',()=>{let x=setup();issue='Port unavailable';x.startLocalExperience('tcp');assert.equal(x.localExperienceText,issue)});
test('Own-server entry creates a separate configuration tab without connecting',()=>{let x=setup();x.prepareOwnConnection();assert.equal(x.created,1);assert.equal(x.connected,0);assert.equal(x.toolsVisible,true);assert.equal(x.activeToolsPanel,'connection')});
test('Stopping the demo releases the runtime and explains the state',()=>{let x=setup();x.stopLocalExperience();assert.equal(stopped,1);assert(x.localExperienceText.includes('已停止'))});
const out={suite:'Production ArkTS local connection entry behavior',total:cases.length,passed:cases.length,skipped:0,cases,sourceSha256:crypto.createHash('sha256').update(src).digest('hex'),scope:'Host behavior; real sockets, SSH and transfer require bound USB evidence'};
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(out,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({total:cases.length,passed:cases.length}));
