async function getJson(target, basePath = "", extraParams) {
	if (typeof window === "undefined") {
		if (!basePath) {
			basePath = 'http://0.0.0.0:8080/';
		}
		if (typeof getJson.fetch === "undefined") {
			getJson.fetch = (...args) => import("node-fetch").then(({default: fetch}) => fetch(...args));
		}
		const data = await getJson.fetch(basePath + target, extraParams);
		return data.json();
	} else {
		const description = await fetch(basePath + target, extraParams);
		return await description.json();
	}
}

function humanise(source) {
	if (source.length == 0)
		return "";
	const isUpperCase = (character) => {
		return character.toUpperCase() == character;
	}
	const isLowerCase = (character) => {
		return character.toLowerCase() == character;
	}
	let result = source[0].toUpperCase(); 
	for (let i = 1; i < source.length; i++) {
		if (source[i] == "_")
			result += " ";
		else if (isUpperCase(source[i])) {
			if (!isUpperCase(source[i - 1])) {
				result += " ";
			}

			if (i + 1 >= source.length || isUpperCase(source[i + 1])) {
				result += source[i];	  
			} else {
				if (isUpperCase(source[i - 1])) {
					result += " ";
				}
				result += source[i].toLowerCase();
			}
		} else {
			result += source[i];
		}
	}
	return result;
}
	
function extractType(type) {
	if (Array.isArray(type) && type.length > 0 ) {
		let extracted = extractType(type[0]);
		return `[${extracted}]`;			
	} else
		return type;
};

function makeGuiForArg(name, type, identifier, types) {
	const typeObtained = type ?? identifier.type;
	let space = document.createElement("div");
	if (typeObtained == "boolean") {
		let checkbox = document.createElement("input");
		checkbox.setAttribute("type", "checkbox");
		checkbox.textContent = humanise(name);
		return [space, () => { checkbox.checked; }];
	}
	
	space.textContent = humanise(name);
	if (typeObtained == "number" || typeObtained == "string") {
		let field = document.createElement("input");
		if (typeObtained == "number")
			field.setAttribute("type", "number");
		space.appendChild(field);
		let getValue = () => {
			if (typeObtained == "number")
				return parseFloat(field.value);
			else
				return field.value;
		};
		return [space, getValue];
	} else if (Array.isArray(typeObtained)) {
		return [space, () => { return []; }];
	} else {
		const made = new types[typeObtained];
		let [element, getter] = made.gui();
		space.appendChild(element);
		return [space, getter];
	}

}
	
function makeCheckIfExpectedType(name, described) {
	if (described == "number") {
		return `if (typeof ${name} != "number") { throw "${name} must be a number" }`;
	} else if (described == "string") {
		return `if (typeof ${name} != "string") { throw "${name} must be a string" }`;
	} else if (described == "boolean") {
		return `if (typeof ${name} != "boolean") { throw "${name} must be a boolean" }`;
	} else if (typeof type == "object" && Array.isArray(type)) {
		let result = `if (typeof ${name} != "object" || Array.isArray(${name})) { throw "${name} must be an array" } `;
		result += `for (let i = 0; i < ${name}.length; i++) { ${ makeCheckIfExpectedType(name + "[i]", described[0]) } }`;
	} else {
		return `if (${name}.constructor.name != "${described}") { throw "${name} must be a an object of type ${described}" }`;
	}
}

function generateApiClass(name, definition, types) {
	let code = `(class ${name} {\n/**\n`;
	code += ` * Creates a new instance of class ${name}\n`;
	for (const [name, type] of Object.entries(definition)) {
		code += ` * @param {${extractType(type)}} ${name}\n`;
	}
	code += " */\n\tconstructor("
	let index = 0;
	for (const [name, type] of Object.entries(definition)) {
		if (index != 0) {
			code += ", ";
		}
		code += `arg_${name} = `;
		if (type == "number") code += "0";
		else if (type == "string") code += "''";
		else if (type == "boolean") code += "false";
		else if (typeof type == "object" && Array.isArray(type)) code += "[]";
		else code += "null";
		index++;
	}
	code += ") {\n"
	for (const [name, type] of Object.entries(definition)) {
		code += `\t\tthis.${name} = arg_${name}\n`;
	}
	code += "\t}\n\ttoJson() {\n";
	code += "\t\tlet result = {}\n";
	
	for (const [name, type] of Object.entries(definition)) {
		code += `\t\t${ makeCheckIfExpectedType(`this.${name}`, type) }\n`;
		code += `\t\tresult.${name} = this.${name}\n`;
	}
	code += "\t\treturn result;\n";
	code += "\t}\n";
	
	code += "\tgui() {\n"
	code += "\t\tlet container = document.createElement('div')\n";
	code += "\t\tlet valueGetters = []\n";
	for (const [name, type] of Object.entries(definition)) {
		code += `\t\tlet [elem_${name}, getter_${name}] = makeGuiForArg("${ humanise(name) }", "${type}", undefined, types)\n`;
		code += `\t\tcontainer.appendChild(elem_${name})\n`;
		code += `\t\tvalueGetters.push(() => this.${name} = getter_${name}())\n`;
	}
	code += "\t\treturn [container, () => { for (const getter of valueGetters) getter(); return this; } ]\n";
	
	code += "\t}\n})";

	let made = eval(code);
	made.source = code;
	return made;
}

function generateApiCall(name, argNames, argDefinitions, docLines, returnInfo, path) {
	if (typeof generateApiCall.messageIndex === "undefined") {
		generateApiCall.messageIndex = 0;
	}
	
	let code = "/**\n";
	if (docLines.length > 0) {
		for (const line of returnInfo.doc_lines) {
			code += ` * ${line}\n`;
		}
		code += " *\n";
	}
	for (let i = 0; i < argNames.length; i++) {
		const type = extractType(argDefinitions[i].type);
		code += ` * @param {${type}} arg_${ argNames[i] } ${ argDefinitions[i].doc_lines.join(" ") } \n`;
	}
	code += ` * @return {${extractType(returnInfo.type)}} ${ returnInfo.doc_lines.join(" ") } \n`;
	
	code += " */\nasync (";
	for (let i = 0; i < argNames.length; i++) {
		if (i != 0) {
			code += ", ";
		}
		code += `arg_${argNames[i]}`;
	}
	code += ") => {\n";
	
	code += `\tconst request = { jsonrpc : '2.0', id : generateApiCall.messageIndex, method : '${name}', params : {} }\n`;
	code += "\tgenerateApiCall.messageIndex++\n";
	for (let i = 0; i < argNames.length; i++) {
		if (argDefinitions[i].optional) {
			code += `\tif (typeof arg_${argNames[i]} != 'undefined') `;
		}
		code += `\t{\n\t\t${ makeCheckIfExpectedType(`arg_${argNames[i]}`, argDefinitions[i].type) }\n`;
		code += `\t\tif (typeof arg_${argNames[i]} == "object" && typeof arg_${argNames[i]}.constructor.name == "string")\n`;
		code += `\t\t\trequest.params.${argNames[i]} = arg_${argNames[i]}.toJson()\n`;
		code += `\t\trequest.params.${argNames[i]} = arg_${argNames[i]}\n`;
		code += "\t}\n";
	}
	code += "\tconst stringified = JSON.stringify(request)\n";
	code += "\tconst response = await getJson('', path, { method : 'POST', cache: 'no-cache', headers: {'Content-Type': 'application/json'}, body : stringified} )\n";
	code += "\tif (response.error) { throw response.error.message; }\n";
	code += "\treturn response.result\n";
	code += "}";
	
	const made = eval(code);
	made.source = code;
	return made;
}

function generateApiCallGui(callee, name, argNames, argDefinitions, types) {
	let questionnaire = document.createElement("details");
	questionnaire.open = true;
	let summary = document.createElement("summary");
	let text = humanise(name);
	summary.textContent = text;
	questionnaire.appendChild(summary);
	
	let argGetters = [];
	for (let i = 0; i < argNames.length; i++) {
		let [element, argGetter] = makeGuiForArg(argNames[i], undefined, argDefinitions[i], types);
		argGetters.push(argGetter);
		questionnaire.appendChild(element);
	}
	
	let button = document.createElement("button");
	let resultSpace = document.createElement("div");
	button.innerHTML = text;
	button.onclick = async function () {
		let args = [];
		for (const getter of argGetters) {
			args.push(getter());
		}
		const result = await callee(...args);
		const resultString = String(result);
		if (typeof resultString == "string" && resultString != "null")
			resultSpace.textContent = resultString;
	};
	
	questionnaire.appendChild(button);
	questionnaire.appendChild(resultSpace);
	return questionnaire;
}

export async function loadApi(path) {
	const description = await getJson("api_description.json");
	let api = {};
	let types = {};
	
	for (const [name, members] of Object.entries(description.types)) {
		types[name] = generateApiClass(name, members, types);
	}
	for (const [name, method] of Object.entries(description.methods)) {
		const nameParts = name.split(".");
		let subApi = api;
		
		let argNames = [];
		let argDefinitions = [];
		for (let i = 0; i < Object.keys(method.params).length; i++) {
			for (const [argName, argDefinition] of Object.entries(method.params)) {
				if (argDefinition.def_order == i + 1) {
					argNames.push(argName);
					argDefinitions.push(argDefinition);
					break;
				}
			}
		}
		
		let made = generateApiCall(name, argNames, argDefinitions, method.doc_lines, method.ret_info, path);
		made.gui = () => generateApiCallGui(made, name, argNames, argDefinitions, types);
		
		for (let i = 0; i < nameParts.length; i++) {
			if (i == nameParts.length - 1) {
				subApi[nameParts[i]] = made;
			} else {
				if (typeof subApi[nameParts[i]] === "undefined") {
					subApi[nameParts[i]] = {};
				}
				subApi = subApi[nameParts[i]];
			}
		}
	}
	
	const makeApiGuiGenerator = (api, name) => {
		let elementMakers = [];
		for (const [subName, value] of Object.entries(api)) {
			if (typeof value != "function") {
				value.gui = makeApiGuiGenerator(value, subName);
			}
			elementMakers.push(value.gui);
		}
		api.gui = () => {
			let container;
			if (name) {
				container = document.createElement("details");
				container.open = true;
				container.appendChild(document.createElement(name));
			} else {
				container = document.createElement("div");
			}
			for (const maker of elementMakers) {
				container.appendChild(maker());
			}
			return container;
		}
	};
	makeApiGuiGenerator(api);
	
	return [api, types, description.servicename];
}
