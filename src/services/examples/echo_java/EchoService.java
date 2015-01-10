import nordugrid.arc.*;

public class EchoService {
    // private NS ns_;
    static {
            // load libjarc.so
            System.loadLibrary("jarc");
    }

    public EchoService() {
        System.out.println("EchoService constructor called");
        // NS ns_ = new NS();
        // ns_.set("echo", "http://www.nordugrid.org/schemas/echo");
    }

/*
    public int process() {
        System.out.println("EchoService process called");
        return 10;
    }
*/
    public MCC_Status process(SOAPMessage inmsg, SOAPMessage outmsg) {
        System.out.println("EchoService process with messages called");
        // XXX: error handling
        SOAPEnvelope in_payload = inmsg.Payload();
        if (in_payload == null) {
            return new MCC_Status(StatusKind.GENERIC_ERROR);
        }
        XMLNode echo = in_payload.Get("echo");
        XMLNode echo_op = new XMLNode(echo);
        String say = new String(echo_op.Get("say").toString());
        System.out.println("Java got: " + say);
        String hear = new String("[ " + say + " ]");
        NS ns_ = new NS();
        ns_.set("echo", "http://www.nordugrid.org/schemas/echo");
        PayloadSOAP outpayload = new PayloadSOAP(ns_);
        outpayload.NewChild("echo:echoResponse").NewChild("echo:hear").Set(hear);
        outmsg.Payload(outpayload);
        return new MCC_Status(StatusKind.STATUS_OK);
    }
}