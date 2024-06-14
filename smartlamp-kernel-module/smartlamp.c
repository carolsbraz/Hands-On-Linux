#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositivo USB

static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saída da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x10C4  // VendorID do smartlamp
#define PRODUCT_ID  0XEA60  // ProductID do smartlamp
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int usb_read_serial(void);   
static int usb_write_serial(char *cmd, int param);
static int usb_send_cmd(char *cmd, int param);

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);

// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct attribute *attrs[] = { &led_attribute.attr, &ldr_attribute.attr, NULL };
static struct attribute_group attr_group = { .attrs = attrs };
static struct kobject *sys_obj; // Executado para ler a saída da porta serial

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",     // Nome do driver
    .probe       = usb_probe,       // Executado quando o dispositivo é conectado na USB
    .disconnect  = usb_disconnect,  // Executado quando o dispositivo é desconectado na USB
    .id_table    = id_table,        // Tabela com o VendorID e ProductID do dispositivo
};

module_usb_driver(smartlamp_driver);

static int usb_write_serial(char *cmd, int param) {
    int ret, actual_size;
    char resp_expected[MAX_RECV_LINE]; // Resposta esperada do comando
    
    // Armazena o comando em formato de texto que o firmware reconheça
    snprintf(usb_out_buffer, usb_max_size, "%s %d\n", cmd, param);

    // Grave o valor de usb_out_buffer com printk
    printk(KERN_INFO "SmartLamp: Enviando comando: %s\n", usb_out_buffer);

    // Envia o comando pela porta Serial
    ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), usb_out_buffer, strlen(usb_out_buffer), &actual_size, 1000);
    if (ret) {
        printk(KERN_ERR "SmartLamp: Erro de código %d ao enviar comando!\n", ret);
        return -1;
    }

    // Use essa variável para fazer a integração com a função usb_read_serial
    // resp_expected deve conter a resposta esperada do comando enviado e deve ser comparada com a resposta recebida
    snprintf(resp_expected, MAX_RECV_LINE, "RES %s", cmd);

    return 0;
}

static int usb_read_serial() {
    int ret, actual_size;
    int retries = 10;  // Tenta algumas vezes receber uma resposta da USB. Depois desiste.
    int X = 0;  // Valor do LDR a ser extraído
    char *start_ptr;

    // Espera pela resposta correta do dispositivo (desiste depois de várias tentativas)
    while (retries > 0) {
        // Lê os dados da porta serial e armazena em usb_in_buffer
        ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, usb_max_size, &actual_size, 1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Código: %d\n", retries--, ret);
            retries--;
            continue;
        }

        // Encerra a string corretamente
        usb_in_buffer[actual_size] = '\0';
        // Procura pela string esperada "RES GET_LDR"
        start_ptr = strstr(usb_in_buffer, "RES GET_LDR ");
        if (start_ptr) {
            // Extrai o valor numérico que segue o "RES GET_LDR "
            sscanf(start_ptr, "RES GET_LDR %d", &X);
            printk(KERN_INFO "SmartLamp: LDR Value Received: %d\n", X);
            return X;  // Retorna o valor do LDR extraído
        } else {
            printk(KERN_INFO "SmartLamp: Mensagem recebida não contém 'RES GET_LDR'\n");
        }

        retries--;
    }

    printk(KERN_ERR "SmartLamp: Falha em receber uma resposta válida após várias tentativas.\n");
    return -1;  // Retorna erro se não conseguir extrair o valor após várias tentativas
}

// Executado quando o dispositivo é conectado na USB
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    // Cria arquivos do /sys/kernel/smartlamp/*
    sys_obj = kobject_create_and_add("smartlamp", kernel_kobj);
    if (!sys_obj) {
        printk(KERN_ERR "SmartLamp: Falha ao criar kobject.\n");
        return -ENOMEM;
    }
    if (sysfs_create_group(sys_obj, &attr_group)) {
        kobject_put(sys_obj);
        printk(KERN_ERR "SmartLamp: Falha ao criar sysfs group.\n");
        return -ENOMEM;
    }

    // Detecta portas e aloca buffers de entrada e saída de dados na USB
    smartlamp_device = interface_to_usbdev(interface);
    if (usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL)) {
        printk(KERN_ERR "SmartLamp: Falha ao encontrar endpoints.\n");
        return -ENODEV;
    }
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    if (!usb_in_buffer || !usb_out_buffer) {
        printk(KERN_ERR "SmartLamp: Falha ao alocar buffers USB.\n");
        return -ENOMEM;
    }

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    kfree(usb_in_buffer);  // Desaloca buffers
    kfree(usb_out_buffer);
    kobject_put(sys_obj);
}

// Envia um comando via USB, espera e retorna a resposta do dispositivo (convertido para int)
static int usb_send_cmd(char *cmd, int param) {
    int result = usb_write_serial(cmd, param);
    if (result < 0) {
        return result;
    }
    return usb_read_serial();
}

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff) {
    int value = -1;
    const char *attr_name = attr->attr.name;

    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    if (strcmp(attr_name, "led") == 0) {
        value = usb_send_cmd("GET_LED", -1);
    } else if (strcmp(attr_name, "ldr") == 0) {
        value = usb_send_cmd("GET_LDR", -1);
    }

    // Implemente a leitura do valor do led usando a função usb_read_serial()
    snprintf(buff, PAGE_SIZE, "%d\n", value); // Cria a mensagem com o valor do led, ldr
    return strlen(buff);
}

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count) {
    long value;
    const char *attr_name = attr->attr.name;
    int result;
    if (strcmp(attr_name, "ldr") == 0) {
        printk(KERN_ERR "SmartLamp: Não é possível escrever no arquivo ldr\n");
        return -EACCES;
    } 

    if (kstrtol(buff, 10, &value)) {
        printk(KERN_ALERT "SmartLamp: valor de %s inválido.\n", attr_name);
        return -EINVAL;
    }

    printk(KERN_INFO "SmartLamp: Setando %s para %ld ...\n", attr_name, value);

    result = usb_send_cmd("SET_LED", value);
    if (result < 0) {
        printk(KERN_ALERT "SmartLamp: erro ao setar o valor do %s.\n", attr_name);
        return result;
    }

    return count;
}
