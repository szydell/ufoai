package net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.ui;

import net.sourceforge.ufoai.ufoscripteditor.parser.IParserContext;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.IUFOSubParser;
import net.sourceforge.ufoai.ufoscripteditor.parser.parsers.subparser.IUFOSubParserFactory;

public class AbstractOptionSubParser extends AbstractNodeSubParser {
	public static final IUFOSubParserFactory FACTORY = new UFONodeParserFactoryAdapter() {
		@Override
		public String getID() {
			return "abstractoption";
		}

		@Override
		public IUFOSubParser create(IParserContext ctx) {
			return new AbstractOptionSubParser(ctx);
		}

	};

	@Override
	public IUFOSubParserFactory getNodeSubParserFactory() {
		return FACTORY;
	}

	public AbstractOptionSubParser(IParserContext ctx) {
		super(ctx);
		IUFOSubParserFactory factory = getNodeSubParserFactory();
		// Registration for event properties
		registerSubParser(EventPropertySubParser.ONVIEWCHANGE, factory);
	}
}
