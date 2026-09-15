import smNoSaccadeStyle from 'sm-no-saccade-style';
import {parse} from 'espree';

export default [
	...smNoSaccadeStyle.configs.recommended
	, {
		files: ['pdo_cfd1_*.js']
		, languageOptions: {
			// Module parsing accepts await in EM_ASYNC_JS function bodies.
			sourceType: 'module'
			, parser: {
				parse(source, options) {
					// Enable function-body returns after ESLint normalizes module options.
					return parse(source, {
						...options
						, ecmaFeatures: {...options.ecmaFeatures, globalReturn: true}
					});
				}
			}
		}
	}
];
