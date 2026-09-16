


CREATE Procedure [dbo].[sp_logitemexchange_insert]
    @NIDMain int,
	@NIDSub int,
	@SGNum int,
	@SvrNum int,
	@FldNum int,
	
	@MakeType int,
	@MakeNum money,
	@ItemAmount int,
	@ItemFromFlag int,
	@ItemFrom int,
	
	@ItemToFlag int,
	@ItemTo int,
	@ExchangeFlag int,
	@Damage int,
	@Defense int,
	
	@Fire int,
	@Ice int,
	@Poison int,
	@Electric int,
	@Spirit int,
	
	@CostumeMID int,
	@CostumeSID int,
	@TradePrice money,    
	@nReturn int OUTPUT
AS
	DECLARE 	
		@error_var int -- Declare variables used in error checking.

	SET NOCOUNT ON

	INSERT INTO LogItemExchange (NIDMain, NIDSub, SGNum, SvrNum, FldNum,
	MakeType, MakeNum, ItemAmount, ItemFromFlag, ItemFrom, 
	ItemToFlag, ItemTo, ExchangeFlag, Damage, Defense,
	Fire, Ice, Poison, Electric, Spirit,
	CostumeMID, CostumeSID, TradePrice) VALUES 
	(@NIDMain, @NIDSub, @SGNum, @SvrNum, @FldNum,
	@MakeType, @MakeNum, @ItemAmount, @ItemFromFlag, @ItemFrom,
	@ItemToFlag, @ItemTo, @ExchangeFlag, @Damage, @Defense,
	@Fire, @Ice, @Poison, @Electric, @Spirit,
	@CostumeMID, @CostumeSID, @TradePrice)

	SELECT @error_var = @@ERROR
	IF @error_var <> 0 
	BEGIN
	    -- 火涝角菩
	    SET @nReturn = 0	    
	END
        ELSE
        BEGIN
	    -- 沥惑利栏肺 火涝 己傍
	    SET @nReturn = 1
        END

	SET NOCOUNT OFF

	RETURN @nReturn	



